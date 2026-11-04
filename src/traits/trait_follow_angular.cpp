#include "traits/trait_follow_angular.h"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/object.hpp>
#include "profiling/myrias_profiling.h"
#include "structs/myrias_entity_streams.h"

namespace myrias {

void kernel_follow_angular(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    MYRIAS_PROFILE_ZONE("Myrias::trait_follow_angular");

    const int slot = 1; // TRAIT_FOLLOW_ANGULAR: bit 9 - 8 = 1
    const auto *cfg = static_cast<const FollowAngularConfig *>(r_group.trait_configs[slot].get());
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

    const float max_turn = cfg->turn_rate * p_dt;
    const bool is_uniform = (r_group.uniform_traits & TRAIT_FOLLOW_ANGULAR) != 0;
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

            const float vx = pvx[j];
            const float vy = pvy[j];
            const float speed_sq = vx * vx + vy * vy;
            if (speed_sq < 1e-6f) {
                continue;
            }

            const float dx = target.x - px[j];
            const float dy = target.y - py[j];
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq < 1e-6f) {
                continue;
            }

            const float speed = Math::sqrt(speed_sq);
            const float current_angle = Math::atan2(vy, vx);
            const float desired_angle = Math::atan2(dy, dx);

            float diff = desired_angle - current_angle;
            while (diff < -(float)godot::Math::PI) diff += (float)godot::Math::TAU;
            while (diff > (float)godot::Math::PI) diff -= (float)godot::Math::TAU;

            const float turn = CLAMP(diff, -max_turn, max_turn);
            const float new_angle = current_angle + turn;

            pvx[j] = Math::cos(new_angle) * speed;
            pvy[j] = Math::sin(new_angle) * speed;
        }
    }
}

} // namespace myrias
