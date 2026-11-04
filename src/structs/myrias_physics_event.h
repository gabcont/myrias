#pragma once

#include <cstdint>
#include "traits/myrias_traits.h"

// Internal group trait flags (bits 0-3 reserved for engine internals)
#define GROUP_FLAG_DIRTY      (1u << 0)
#define GROUP_FLAGS_RESERVED  (0x0Eu)
#define GROUP_FLAGS_INTERNAL  (0x0Fu)
#define GROUP_FLAGS_USER_MASK (~0x0Fu)

#define GROUP_IS_DIRTY(t)     (((t) & GROUP_FLAG_DIRTY) != 0)
#define GROUP_SET_DIRTY(t)    ((t) |= GROUP_FLAG_DIRTY)
#define GROUP_CLEAR_DIRTY(t)  ((t) &= ~GROUP_FLAG_DIRTY)

// User-facing physics traits (bits 4-7)
enum MyriasPhysicsTrait : uint32_t {
    TRAIT_NONE           = 0,
    TRAIT_SEPARATION     = 1u << 4, // Soft separation / push-out
    TRAIT_BOUNCE         = 1u << 5, // Reflect velocity on collision
    TRAIT_DESPAWN_ON_HIT = 1u << 6, // Despawn entity on collision
    TRAIT_ANIM_ON_HIT    = 1u << 7, // Change animation on collision
};
using EntityPhysicsTraits = MyriasPhysicsTrait;

// Deferred physics event emitted during collision/physics resolution,
// processed by MyriasServer on the next frame.
struct MyriasPhysicsEvent {
    enum Type : uint8_t {
        CHANGE_ANIMATION,
        DESPAWN,
        COLLISION,
    };

    Type type = COLLISION;
    uint16_t group_id = 0;
    uint16_t local_idx = 0;
    uint16_t other_group_id = 0;
    uint16_t other_local_idx = 0;
    int param = 0; // Target animation index for CHANGE_ANIMATION, or custom payload.
};
