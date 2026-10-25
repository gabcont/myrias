#pragma once

#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "resources/myrias_entity_model.h"

using namespace godot;

// A deferred request to the MyriasServer. Executed in order at the start of the next process().
struct MyriasCommand {
    enum Type : uint8_t {
        CREATE_GROUP,
        REMOVE_GROUP,
        SPAWN,
        DESPAWN,
    };

    Type type = SPAWN;
    int group_id = -1;

    // CREATE_GROUP
    Ref<MyriasEntityModel> model;
    int pool_size = 0;
    RID ci;

    // SPAWN
    Vector2 pos;
    Vector2 vel;
    int animation = 0;
    float scale = 1.0f;
    float rot = 0.0f;

    // DESPAWN: local index as of the last processed frame.
    int local_index = -1;
};
