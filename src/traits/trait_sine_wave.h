#pragma once

#include "traits/myrias_traits.h"

struct SineWaveConfig : public TraitConfig {
    float amplitude = 20.0f;
    float frequency = 2.0f;
    float phase = 0.0f;
};

namespace myrias {

void kernel_sine_wave(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
