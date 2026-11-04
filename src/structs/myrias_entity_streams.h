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

static constexpr uint32_t DEFAULT_SOA_CAPACITY = 120000;

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
    Vector<float> rot;
    Vector<float> scale;
    Vector<float> lifetime;
    Vector<uint16_t> cur_animation;
    Vector<uint16_t> cur_frame;

    uint32_t m_capacity = 0;
    uint32_t m_used = 0;

    bool set_capacity(uint32_t p_capacity) {
        if (p_capacity < m_used) {
            return false;
        }
        if (phys_x.resize(p_capacity) != OK ||
                phys_y.resize(p_capacity) != OK ||
                phys_vel_x.resize(p_capacity) != OK ||
                phys_vel_y.resize(p_capacity) != OK ||
                rot.resize(p_capacity) != OK ||
                scale.resize(p_capacity) != OK ||
                lifetime.resize(p_capacity) != OK ||
                cur_animation.resize(p_capacity) != OK ||
                cur_frame.resize(p_capacity) != OK) {
            phys_x.resize(m_capacity);
            phys_y.resize(m_capacity);
            phys_vel_x.resize(m_capacity);
            phys_vel_y.resize(m_capacity);
            rot.resize(m_capacity);
            scale.resize(m_capacity);
            lifetime.resize(m_capacity);
            cur_animation.resize(m_capacity);
            cur_frame.resize(m_capacity);
            return false;
        }
        m_capacity = p_capacity;
        return true;
    }

    bool allocate_slice(uint16_t p_pool_size, uint32_t &r_offset) {
        if (p_pool_size == 0 || m_used + p_pool_size > m_capacity) {
            return false;
        }
        r_offset = m_used;
        m_used += p_pool_size;
        return true;
    }

    void free_slice(uint32_t p_offset, uint16_t p_pool_size, Vector<GroupContext> &r_groups, uint16_t p_removed_group_idx) {
        MYRIAS_PROFILE_ZONE("EntitySoA::free_slice");
        if (p_pool_size == 0 || p_offset + p_pool_size > m_used) {
            return;
        }

        GroupContext *groups = r_groups.ptrw();
        for (int i = 0; i < r_groups.size(); i++) {
            if (i == p_removed_group_idx) {
                continue;
            }
            if (groups[i].offset > p_offset) {
                const uint32_t old_off = groups[i].offset;
                const uint32_t new_off = old_off - p_pool_size;
                const uint32_t active_n = groups[i].last_active;
                if (active_n > 0) {
                    std::memmove(phys_x.ptrw() + new_off, phys_x.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(phys_y.ptrw() + new_off, phys_y.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(phys_vel_x.ptrw() + new_off, phys_vel_x.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(phys_vel_y.ptrw() + new_off, phys_vel_y.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(rot.ptrw() + new_off, rot.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(scale.ptrw() + new_off, scale.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(lifetime.ptrw() + new_off, lifetime.ptr() + old_off, active_n * sizeof(float));
                    std::memmove(cur_animation.ptrw() + new_off, cur_animation.ptr() + old_off, active_n * sizeof(uint16_t));
                    std::memmove(cur_frame.ptrw() + new_off, cur_frame.ptr() + old_off, active_n * sizeof(uint16_t));
                }
                groups[i].offset = new_off;
            }
        }

        m_used -= p_pool_size;
    }

    void compact_group(uint32_t p_offset, uint64_t *r_alive_bits, float *r_render_buffer, Vector<uint64_t> *r_trait_bits, uint16_t &r_active_amount, uint16_t &r_last_active) {
        MYRIAS_PROFILE_ZONE("EntitySoA::compact_group");
        float *px = phys_x.ptrw() + p_offset;
        float *py = phys_y.ptrw() + p_offset;
        float *pvx = phys_vel_x.ptrw() + p_offset;
        float *pvy = phys_vel_y.ptrw() + p_offset;
        float *prot = rot.ptrw() + p_offset;
        float *pscale = scale.ptrw() + p_offset;
        float *plifetime = lifetime.ptrw() + p_offset;
        uint16_t *panim = cur_animation.ptrw() + p_offset;
        uint16_t *pframe = cur_frame.ptrw() + p_offset;

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
                    prot[write] = prot[read];
                    pscale[write] = pscale[read];
                    plifetime[write] = plifetime[read];
                    panim[write] = panim[read];
                    pframe[write] = pframe[read];
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
