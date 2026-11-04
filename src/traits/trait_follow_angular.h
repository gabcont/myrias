#pragma once

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include "traits/myrias_traits.h"

using namespace godot;

struct FollowAngularConfig : public TraitConfig {
    Vector2 target_pos = Vector2();
    ObjectID target_node_id;
    float turn_rate = 3.14159265f;
};

namespace myrias {

void kernel_follow_angular(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
