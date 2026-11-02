#pragma once

#include <cstring>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include "../profiling/myrias_profiling.h"
#include "structs/myrias_physics_event.h"
#include "structs/myrias_sprite_table.h"
#include "traits/myrias_traits.h"

using namespace godot;

struct RenderingStream {
    float rot, scale;
    int z_index;
    uint16_t cur_animation, cur_frame;
};

struct LogicStream {
    uint64_t spawn_seq = 0; // Global, monotonically increasing. Newer = drawn on top.
    float lifetime = 0.0f;  // Seconds since spawn. Drives the animation frame.
};

static inline bool ALIVE_TEST(const uint64_t *bits, uint32_t i) {
    return (bits[i >> 6] >> (i & 63)) & 1ull;
}

static inline void ALIVE_SET(uint64_t *bits, uint32_t i) {
    bits[i >> 6] |= (1ull << (i & 63));
}

static inline void ALIVE_CLEAR(uint64_t *bits, uint32_t i) {
    bits[i >> 6] &= ~(1ull << (i & 63));
}

struct GroupContext {
    uint16_t group_id = 0;
    uint32_t offset = 0;
    uint16_t active_amount = 0;
    uint16_t last_active = 0;
    uint16_t pool_size = 0;
    Vector<uint64_t> alive_bits;
    float radius = 8.0f;
    uint16_t collision_layer = 1;
    uint16_t collision_mask = 1;
    uint32_t traits = 0;
    uint32_t default_traits = 0;
    uint32_t uniform_traits = 0;
    Vector<uint64_t> trait_bits[TRAIT_BEHAVIOR_COUNT];
    std::shared_ptr<TraitConfig> trait_configs[TRAIT_BEHAVIOR_COUNT];

    RID ci;        // CanvasItem of the owning node.
    RID multimesh; // MultiMesh drawn on `ci`.
    RID texture;   // Spritesheet.
    RID shader;    // Per-group shader/material.
    RID material;

    // Flattened frame table: frame_table[anims[anim].start + frame].
    FrameTable frame_table;

    PackedFloat32Array buffer; // MultiMesh buffer.
};

struct EntitySoA {
    Vector<float> phys_x;
    Vector<float> phys_y;
    Vector<float> phys_vel_x;
    Vector<float> phys_vel_y;
    Vector<RenderingStream> rendering;
    Vector<LogicStream> logic;

    void reserve(uint32_t p_capacity) {
        phys_x.reserve((int64_t)p_capacity);
        phys_y.reserve((int64_t)p_capacity);
        phys_vel_x.reserve((int64_t)p_capacity);
        phys_vel_y.reserve((int64_t)p_capacity);
        rendering.reserve((int64_t)p_capacity);
        logic.reserve((int64_t)p_capacity);
    }

    bool allocate_slice(uint16_t p_pool_size, uint32_t &r_offset) {
        const uint32_t offset = (uint32_t)phys_x.size();
        const uint32_t new_size = offset + p_pool_size;
        if (phys_x.resize(new_size) != OK ||
                phys_y.resize(new_size) != OK ||
                phys_vel_x.resize(new_size) != OK ||
                phys_vel_y.resize(new_size) != OK ||
                rendering.resize(new_size) != OK ||
                logic.resize(new_size) != OK) {
            phys_x.resize(offset);
            phys_y.resize(offset);
            phys_vel_x.resize(offset);
            phys_vel_y.resize(offset);
            rendering.resize(offset);
            logic.resize(offset);
            return false;
        }
        std::memset(phys_x.ptrw() + offset, 0, p_pool_size * sizeof(float));
        std::memset(phys_y.ptrw() + offset, 0, p_pool_size * sizeof(float));
        std::memset(phys_vel_x.ptrw() + offset, 0, p_pool_size * sizeof(float));
        std::memset(phys_vel_y.ptrw() + offset, 0, p_pool_size * sizeof(float));
        std::memset(logic.ptrw() + offset, 0, p_pool_size * sizeof(LogicStream));
        r_offset = offset;
        return true;
    }

    void free_slice(uint32_t p_offset, uint16_t p_pool_size, Vector<GroupContext> &r_groups, uint16_t p_removed_group_idx) {
        MYRIAS_PROFILE_ZONE("EntitySoA::free_slice");
        const uint32_t total = (uint32_t)phys_x.size();
        if (p_offset + p_pool_size > total || p_pool_size == 0) {
            return;
        }
        const uint32_t slice_end = p_offset + p_pool_size;
        if (slice_end < total) {
            const uint32_t count_after = total - slice_end;
            std::memmove(phys_x.ptrw() + p_offset, phys_x.ptr() + slice_end, count_after * sizeof(float));
            std::memmove(phys_y.ptrw() + p_offset, phys_y.ptr() + slice_end, count_after * sizeof(float));
            std::memmove(phys_vel_x.ptrw() + p_offset, phys_vel_x.ptr() + slice_end, count_after * sizeof(float));
            std::memmove(phys_vel_y.ptrw() + p_offset, phys_vel_y.ptr() + slice_end, count_after * sizeof(float));
            std::memmove(rendering.ptrw() + p_offset, rendering.ptr() + slice_end, count_after * sizeof(RenderingStream));
            std::memmove(logic.ptrw() + p_offset, logic.ptr() + slice_end, count_after * sizeof(LogicStream));
        }
        const uint32_t new_size = total - p_pool_size;
        phys_x.resize(new_size);
        phys_y.resize(new_size);
        phys_vel_x.resize(new_size);
        phys_vel_y.resize(new_size);
        rendering.resize(new_size);
        logic.resize(new_size);

        GroupContext *groups = r_groups.ptrw();
        for (int i = 0; i < r_groups.size(); i++) {
            if (i == p_removed_group_idx) {
                continue;
            }
            if (groups[i].offset > p_offset) {
                groups[i].offset -= p_pool_size;
            }
        }
    }

    void compact_group(uint32_t p_offset, uint64_t *r_alive_bits, float *r_render_buffer, Vector<uint64_t> *r_trait_bits, uint16_t &r_active_amount, uint16_t &r_last_active) {
        MYRIAS_PROFILE_ZONE("EntitySoA::compact_group");
        float *px = phys_x.ptrw() + p_offset;
        float *py = phys_y.ptrw() + p_offset;
        float *pvx = phys_vel_x.ptrw() + p_offset;
        float *pvy = phys_vel_y.ptrw() + p_offset;
        RenderingStream *rend = rendering.ptrw() + p_offset;
        LogicStream *logic_stream = logic.ptrw() + p_offset;

        uint16_t write = 0;
        const uint16_t old_last = r_last_active;
        const uint32_t num_words = (old_last + 63) / 64;

        for (uint32_t w = 0; w < num_words; w++) {
            uint64_t word = r_alive_bits[w];
            if (w == num_words - 1 && (old_last & 63) != 0) {
                word &= (1ull << (old_last & 63)) - 1ull;
            }
            while (word) {
                const uint32_t read = (w << 6) + __builtin_ctzll(word);
                word &= word - 1;

                if (write != read) {
                    px[write] = px[read];
                    py[write] = py[read];
                    pvx[write] = pvx[read];
                    pvy[write] = pvy[read];
                    rend[write] = rend[read];
                    logic_stream[write] = logic_stream[read];
                    std::memcpy(r_render_buffer + (size_t)write * 12, r_render_buffer + (size_t)read * 12, 12 * sizeof(float));

                    if (r_trait_bits != nullptr) {
                        for (uint32_t s = 0; s < TRAIT_BEHAVIOR_COUNT; s++) {
                            if (!r_trait_bits[s].is_empty()) {
                                uint64_t *tbits = r_trait_bits[s].ptrw();
                                if (ALIVE_TEST(tbits, read)) {
                                    ALIVE_SET(tbits, write);
                                } else {
                                    ALIVE_CLEAR(tbits, write);
                                }
                            }
                        }
                    }
                }
                write++;
            }
        }

        // Rewrite bitset: first `write` bits set, rest up to `num_words` cleared
        const uint32_t full_words = write >> 6;
        for (uint32_t w = 0; w < full_words; w++) {
            r_alive_bits[w] = ~0ull;
        }
        const uint32_t rem = write & 63;
        if (full_words < num_words) {
            r_alive_bits[full_words] = rem ? ((1ull << rem) - 1ull) : 0ull;
            for (uint32_t w = full_words + 1; w < num_words; w++) {
                r_alive_bits[w] = 0ull;
            }
        }

        if (r_trait_bits != nullptr) {
            for (uint32_t s = 0; s < TRAIT_BEHAVIOR_COUNT; s++) {
                if (!r_trait_bits[s].is_empty()) {
                    uint64_t *tbits = r_trait_bits[s].ptrw();
                    for (uint32_t k = write; k < old_last; k++) {
                        ALIVE_CLEAR(tbits, k);
                    }
                }
            }
        }

        r_active_amount = write;
        r_last_active = write;
    }
};
