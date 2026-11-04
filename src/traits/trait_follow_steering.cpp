#include "traits/trait_follow_steering.h"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/object.hpp>
#include "profiling/myrias_profiling.h"
#include "structs/myrias_entity_streams.h"

namespace myrias {

void kernel_follow_steering(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    MYRIAS_PROFILE_ZONE("Myrias::trait_follow_steering");

    const int slot = 0; // TRAIT_FOLLOW_STEERING: bit 8 - 8 = 0
    const auto *cfg = static_cast<const FollowSteeringConfig *>(r_group.trait_configs[slot].get());
    if (cfg == nullptr) {
        return;
    }

    Vector2 target = cfg->target_pos;
    if (cfg->target_node_id.is_valid()) {
        Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(cfg->target_node_id));
        if (node != nullptr && node->is_inside_tree()) {
            target = node->get_global_position();
        }
    }

    const float turn_force = cfg->turn_force;
    const float max_speed = cfg->max_speed;
    const float max_speed_sq = max_speed * max_speed;
    const float force_dt = turn_force * p_dt;

    const bool is_uniform = (r_group.uniform_traits & TRAIT_FOLLOW_STEERING) != 0;
    const uint64_t *tbits = is_uniform ? nullptr : (r_group.trait_bits[slot].is_empty() ? nullptr : r_group.trait_bits[slot].ptr());
    if (!is_uniform && tbits == nullptr) {
        return;
    }

    const uint64_t *abits = r_group.alive_bits.ptr();
    const uint32_t num_words = (r_group.last_active + 63) / 64;
    const float *px = r_soa.phys_x.ptr() + r_group.offset;
    const float *py = r_soa.phys_y.ptr() + r_group.offset;
    float *pvx = r_soa.phys_vel_x.ptrw() + r_group.offset;
    float *pvy = r_soa.phys_vel_y.ptrw() + r_group.offset;

    for (uint32_t w = 0; w < num_words; w++) {
        uint64_t word = abits[w];
        if (!is_uniform) {
            word &= tbits[w];
        }
        if (w == num_words - 1 && (r_group.last_active & 63) != 0) {
            word &= (1ull << (r_group.last_active & 63)) - 1ull;
        }

        while (word) {
            const uint32_t j = (w << 6) + __builtin_ctzll(word);
            word &= word - 1;

            const float dx = target.x - px[j];
            const float dy = target.y - py[j];
            const float dist_sq = dx * dx + dy * dy;

            if (dist_sq > 1e-6f) {
                const float inv_dist = 1.0f / Math::sqrt(dist_sq);
                pvx[j] += dx * inv_dist * force_dt;
                pvy[j] += dy * inv_dist * force_dt;
            }

            if (max_speed > 0.0f) {
                const float v_sq = pvx[j] * pvx[j] + pvy[j] * pvy[j];
                if (v_sq > max_speed_sq) {
                    const float inv_v = max_speed / Math::sqrt(v_sq);
                    pvx[j] *= inv_v;
                    pvy[j] *= inv_v;
                }
            }
        }
    }
}

} // namespace myrias
