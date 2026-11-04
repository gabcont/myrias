#pragma once

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include "traits/myrias_traits.h"

using namespace godot;

struct FaceNodeConfig : public TraitConfig {
    Vector2 target_pos = Vector2();
    ObjectID target_node_id;
    float angle_offset = 0.0f; // Radians
};

namespace myrias {

void kernel_face_node(GroupContext &r_group, EntitySoA &r_soa, float p_dt);

} // namespace myrias
