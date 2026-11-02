#pragma once

#include <memory>

#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "structs/myrias_sprite_table.h"
#include "traits/myrias_traits.h"

using namespace godot;

// A deferred request to the MyriasServer. Executed in order at the start of the next process().
struct MyriasCommand {
    enum Type : uint8_t {
        CREATE_GROUP,
        REMOVE_GROUP,
        SPAWN,
        DESPAWN,
        CONFIGURE_TRAIT,
        REMOVE_TRAIT,
        SET_ENTITY_TRAIT,
    };

    Type type = SPAWN;
    uint16_t group_id = 0;

    // CREATE_GROUP
    FrameTable frame_table;
    RID texture;
    float radius = 8.0f;
    uint16_t collision_layer = 1;
    uint16_t collision_mask = 1;
    uint32_t traits = 0;
    uint16_t pool_size = 0;
    RID ci;

    // SPAWN
    Vector2 pos;
    Vector2 vel;
    int animation = 0;
    float scale = 1.0f;
    float rot = 0.0f;

    // DESPAWN / SET_ENTITY_TRAIT: local index as of the last processed frame.
    int local_index = -1;

    // TRAIT COMMANDS
    uint32_t trait_flag = 0;
    bool trait_enabled = false;
    std::shared_ptr<TraitConfig> trait_cfg = nullptr;
};
