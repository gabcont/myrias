#pragma once

#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/ref.hpp>
#include "traits/myrias_traits.h"

using namespace godot;

struct CurveModifierConfig : public TraitConfig {
    static constexpr int LUT_SIZE = 64;
    float lut[LUT_SIZE] = {};
    int target_property = 0; // 0: Scale, 1: Rotation, 2: Speed
    float duration = 1.0f;
    float multiplier = 1.0f;
    bool loop = false;

    void bake(const Ref<Curve> &p_curve) {
        if (p_curve.is_valid()) {
            const_cast<Curve *>(p_curve.ptr())->bake();
            for (int i = 0; i < LUT_SIZE; i++) {
                const float offset = (float)i / (float)(LUT_SIZE - 1);
                lut[i] = p_curve->sample_baked(offset);
            }
        } else {
            for (int i = 0; i < LUT_SIZE; i++) {
                lut[i] = 1.0f;
            }
        }
    }
};

namespace myrias {

void kernel_curve_modifier(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
