#pragma once

#include "traits/myrias_traits.h"

struct PhysicsTraitConfig : public TraitConfig {
    float mass = 1.0f;
    float inv_mass = 1.0f;
    float bounce = 0.5f;
    float friction = 0.2f;
    float gravity_scale = 1.0f;
    float linear_damp = 0.0f;
};
