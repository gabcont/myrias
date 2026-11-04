#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

enum MyriasTraitType : uint32_t {
    TRAIT_FOLLOW_STEERING = 1u << 8,
    TRAIT_FOLLOW_ANGULAR  = 1u << 9,
    TRAIT_SINE_WAVE       = 1u << 10,
    TRAIT_CURVE_MODIFIER  = 1u << 11,
    TRAIT_FACE_VELOCITY   = 1u << 12,
    TRAIT_FACE_NODE       = 1u << 13,
};

static constexpr uint32_t TRAIT_BEHAVIOR_FIRST_BIT = 8;
static constexpr uint32_t TRAIT_BEHAVIOR_COUNT = 6;
static constexpr uint32_t TRAIT_BEHAVIOR_MASK = 0x3F00u;

enum MyriasCurveTarget : int {
    CURVE_TARGET_SCALE = 0,
    CURVE_TARGET_ROTATION = 1,
    CURVE_TARGET_SPEED = 2,
};

struct TraitConfig {
    virtual ~TraitConfig() = default;
};

struct GroupContext;
struct EntitySoA;

namespace myrias {

using TraitKernelFn = void (*)(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

Vector2 get_frame_size(const GroupContext &g, uint16_t anim_idx, uint16_t frame_idx);
void run_group_traits(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
