#include "traits/myrias_traits.h"

#include "structs/myrias_entity_streams.h"
#include "traits/trait_follow_steering.h"
#include "traits/trait_follow_angular.h"
#include "traits/trait_sine_wave.h"
#include "traits/trait_curve_modifier.h"
#include "traits/trait_face_velocity.h"
#include "traits/trait_face_node.h"

namespace myrias {

Vector2 get_frame_size(const GroupContext &g, uint16_t anim_idx, uint16_t frame_idx) {
    if (g.frame_table.anims.is_empty() || g.frame_table.frames.is_empty()) {
        return Vector2(1.0f, 1.0f);
    }
    const int anim = CLAMP((int)anim_idx, 0, g.frame_table.anims.size() - 1);
    const AnimInfo &ai = g.frame_table.anims[anim];
    if (ai.count <= 0) {
        return Vector2(1.0f, 1.0f);
    }
    const FrameInfo &fi = g.frame_table.frames[ai.start + MIN((int)frame_idx, ai.count - 1)];
    return fi.size;
}

static void kernel_noop(GroupContext &, EntitySoA &, float) {}

static const TraitKernelFn s_trait_kernels[32] = {
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_follow_steering, // 8
    kernel_follow_angular,  // 9
    kernel_sine_wave,       // 10
    kernel_curve_modifier,  // 11
    kernel_face_velocity,   // 12
    kernel_face_node,       // 13
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_noop, kernel_noop, kernel_noop, kernel_noop,
    kernel_noop, kernel_noop
};

void run_group_traits(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    uint32_t mask = r_group.traits & TRAIT_LOGIC_MASK;
    while (mask) {
        const uint32_t bit = __builtin_ctz(mask);
        mask &= mask - 1;
        s_trait_kernels[bit](r_group, r_soa, p_dt);
    }
}

} // namespace myrias
