#pragma once

#include "traits/myrias_traits.h"

struct FaceVelocityConfig : public TraitConfig {
    float angle_offset = 0.0f; // Radians
};

namespace myrias {

void kernel_face_velocity(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
