#pragma once

#include <cstdint>
#include <cstring>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "structs/myrias_entity_streams.h"
#include "profiling/myrias_profiling.h"

using namespace godot;

// 32-byte cache-aligned physics proxy representing an entity inside the spatial grid.
struct alignas(32) PhysicsProxy {
    float x, y;
    float vel_x, vel_y;
    float radius;
    uint16_t collision_layer;
    uint16_t collision_mask;
    uint32_t traits;
    uint16_t group_id;
    uint16_t local_idx;
};

// Bounded flat uniform spatial grid built using counting-sort (zero per-frame heap allocations).
class MyriasSpatialGrid {
private:
    float m_origin_x = 0.0f;
    float m_origin_y = 0.0f;
    float m_cell_size = 32.0f;
    float m_inv_cell_size = 1.0f / 32.0f;
    int m_cols = 64;
    int m_rows = 36;
    float m_max_radius = 0.0f;

    Vector<int> m_cell_start;
    Vector<int> m_cell_count;
    Vector<int> m_cell_cursor;

    Vector<PhysicsProxy> m_unsorted_proxies;
    Vector<int> m_proxy_cells;
    Vector<PhysicsProxy> m_sorted_proxies;

    inline int world_to_cell_x(float p_x) const {
        int cx = (int)((p_x - m_origin_x) * m_inv_cell_size);
        return CLAMP(cx, 0, m_cols - 1);
    }

    inline int world_to_cell_y(float p_y) const {
        int cy = (int)((p_y - m_origin_y) * m_inv_cell_size);
        return CLAMP(cy, 0, m_rows - 1);
    }

public:
    MyriasSpatialGrid() {
        configure(Rect2(0.0f, 0.0f, 2048.0f, 1152.0f), 32.0f);
    }

    void configure(const Rect2 &p_bounds, float p_cell_size) {
        m_cell_size = MAX(p_cell_size, 1.0f);
        m_inv_cell_size = 1.0f / m_cell_size;
        m_origin_x = p_bounds.position.x;
        m_origin_y = p_bounds.position.y;
        m_cols = MAX((int)Math::ceil(p_bounds.size.x * m_inv_cell_size), 1);
        m_rows = MAX((int)Math::ceil(p_bounds.size.y * m_inv_cell_size), 1);

        const int num_cells = m_cols * m_rows;
        m_cell_start.resize(num_cells);
        m_cell_start.fill(0);
        m_cell_count.resize(num_cells);
        m_cell_count.fill(0);
        m_cell_cursor.resize(num_cells);
        m_cell_cursor.fill(0);
    }

    void ensure_capacity(int p_total_capacity) {
        if (m_unsorted_proxies.size() != p_total_capacity) {
            m_unsorted_proxies.resize(p_total_capacity);
            m_proxy_cells.resize(p_total_capacity);
            m_sorted_proxies.resize(p_total_capacity);
        }
    }

    // Rebuilds the sorted proxy array from the SoA in O(N + Cells) time with zero allocations.
    int build(const EntitySoA &p_soa, const Vector<GroupContext> &p_groups) {
        MYRIAS_PROFILE_ZONE("SpatialGrid::build");
        const int num_cells = m_cols * m_rows;
        if (num_cells <= 0) {
            return 0;
        }

        int *counts = m_cell_count.ptrw();
        std::memset(counts, 0, sizeof(int) * num_cells);

        PhysicsProxy *unsorted = m_unsorted_proxies.ptrw();
        int *cells = m_proxy_cells.ptrw();
        const float *px_base = p_soa.phys_x.ptr();
        const float *py_base = p_soa.phys_y.ptr();
        const float *pvx_base = p_soa.phys_vel_x.ptr();
        const float *pvy_base = p_soa.phys_vel_y.ptr();
        const float *scale_base = p_soa.scale.ptr();

        int total = 0;
        float max_r = 0.0f;

        // 1. Gather proxies from SoA & count entities per cell
        for (int g_idx = 0; g_idx < p_groups.size(); g_idx++) {
            const GroupContext &g = p_groups[g_idx];
            if (g.radius <= 0.0f || g.last_active == 0 || g.active_amount == 0) {
                continue;
            }

            const float *px = px_base + g.offset;
            const float *py = py_base + g.offset;
            const float *pvx = pvx_base + g.offset;
            const float *pvy = pvy_base + g.offset;
            const float *scale = scale_base + g.offset;
            const uint64_t *bits = g.alive_bits.ptr();
            const float base_r = g.radius;
            const uint16_t layer = g.collision_layer;
            const uint16_t mask = g.collision_mask;
            const uint32_t traits = g.traits;
            const uint16_t gid = (uint16_t)g.group_id;

            const uint32_t num_words = (g.last_active + 63) / 64;
            for (uint32_t w = 0; w < num_words; w++) {
                uint64_t word = bits[w];
                if (w == num_words - 1 && (g.last_active & 63) != 0) {
                    word &= (1ull << (g.last_active & 63)) - 1ull;
                }
                while (word) {
                    const uint32_t j = (w << 6) + __builtin_ctzll(word);
                    word &= word - 1;

                    const float x = px[j];
                    const float y = py[j];
                    const float vx = pvx[j];
                    const float vy = pvy[j];
                    const float r = base_r * scale[j];
                    if (r > max_r) {
                        max_r = r;
                    }

                    const int cx = world_to_cell_x(x);
                    const int cy = world_to_cell_y(y);
                    const int cell = cy * m_cols + cx;

                    unsorted[total] = { x, y, vx, vy, r, layer, mask, traits, gid, (uint16_t)j };
                    cells[total] = cell;
                    counts[cell]++;
                    total++;
                }
            }
        }

        m_max_radius = max_r;
        if (total == 0) {
            std::memset(m_cell_start.ptrw(), 0, sizeof(int) * num_cells);
            return 0;
        }

        // 2. Prefix sum to compute cell_start and initialize cell_cursor
        int *starts = m_cell_start.ptrw();
        int *cursors = m_cell_cursor.ptrw();
        int accum = 0;
        for (int c = 0; c < num_cells; c++) {
            starts[c] = accum;
            cursors[c] = accum;
            accum += counts[c];
        }

        // 3. Scatter proxies into cell-sorted order
        PhysicsProxy *sorted = m_sorted_proxies.ptrw();
        for (int i = 0; i < total; i++) {
            const int cell = cells[i];
            sorted[cursors[cell]++] = unsorted[i];
        }

        return total;
    }

    // Writes modified proxy positions and velocities back into the SoA.
    void sync_to_soa(EntitySoA &r_soa, const Vector<GroupContext> &p_groups, int p_total_proxies) const {
        MYRIAS_PROFILE_ZONE("SpatialGrid::sync_to_soa");
        if (p_total_proxies <= 0) {
            return;
        }
        float *px_base = r_soa.phys_x.ptrw();
        float *py_base = r_soa.phys_y.ptrw();
        float *pvx_base = r_soa.phys_vel_x.ptrw();
        float *pvy_base = r_soa.phys_vel_y.ptrw();
        const PhysicsProxy *sorted = m_sorted_proxies.ptr();

        for (int i = 0; i < p_total_proxies; i++) {
            const PhysicsProxy &px = sorted[i];
            for (int g_idx = 0; g_idx < p_groups.size(); g_idx++) {
                const GroupContext &g = p_groups[g_idx];
                if (g.group_id == px.group_id) {
                    const uint32_t abs_idx = g.offset + px.local_idx;
                    px_base[abs_idx] = px.x;
                    py_base[abs_idx] = px.y;
                    pvx_base[abs_idx] = px.vel_x;
                    pvy_base[abs_idx] = px.vel_y;
                    break;
                }
            }
        }
    }

    // Queries whether any entity matching p_collision_mask (and p_group_id if >= 0) overlaps (p_pos, p_radius).
    bool query_collision(Vector2 p_pos, float p_radius, int p_group_id = -1, uint16_t p_collision_mask = 0xFFFF) const {
        MYRIAS_PROFILE_ZONE("SpatialGrid::query_collision");
        if (m_cols <= 0 || m_rows <= 0 || m_max_radius <= 0.0f || m_sorted_proxies.is_empty()) {
            return false;
        }

        const float reach = MAX(p_radius, 0.0f) + m_max_radius;
        const int min_cx = world_to_cell_x(p_pos.x - reach);
        const int max_cx = world_to_cell_x(p_pos.x + reach);
        const int min_cy = world_to_cell_y(p_pos.y - reach);
        const int max_cy = world_to_cell_y(p_pos.y + reach);

        const int *starts = m_cell_start.ptr();
        const int *counts = m_cell_count.ptr();
        const PhysicsProxy *sorted = m_sorted_proxies.ptr();

        for (int cy = min_cy; cy <= max_cy; cy++) {
            const int row_offset = cy * m_cols;
            for (int cx = min_cx; cx <= max_cx; cx++) {
                const int cell = row_offset + cx;
                const int start = starts[cell];
                const int end = start + counts[cell];

                for (int i = start; i < end; i++) {
                    const PhysicsProxy &px = sorted[i];
                    if ((px.collision_layer & p_collision_mask) == 0) {
                        continue;
                    }
                    if (p_group_id >= 0 && px.group_id != (uint16_t)p_group_id) {
                        continue;
                    }
                    const float dx = px.x - p_pos.x;
                    const float dy = px.y - p_pos.y;
                    const float r_sum = p_radius + px.radius;
                    if (dx * dx + dy * dy <= r_sum * r_sum) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    Vector<PhysicsProxy> &get_sorted_proxies() { return m_sorted_proxies; }
    const Vector<PhysicsProxy> &get_sorted_proxies() const { return m_sorted_proxies; }
};
