#include "myrias_entity_model.h"

void MyriasEntityModel::set_sprite_frames(const Ref<SpriteFrames> &p_frames) {
    m_sprite_frames = p_frames;
}

Ref<SpriteFrames> MyriasEntityModel::get_sprite_frames() const {
    return m_sprite_frames;
}

void MyriasEntityModel::set_radius(float p_radius) {
    m_radius = MAX(p_radius, 0.0f);
}

float MyriasEntityModel::get_radius() const {
    return m_radius;
}

void MyriasEntityModel::set_collision_layer(uint32_t p_layer) {
    m_collision_layer = (uint16_t)(p_layer & 0xFFFF);
}

uint32_t MyriasEntityModel::get_collision_layer() const {
    return m_collision_layer;
}

void MyriasEntityModel::set_collision_mask(uint32_t p_mask) {
    m_collision_mask = (uint16_t)(p_mask & 0xFFFF);
}

uint32_t MyriasEntityModel::get_collision_mask() const {
    return m_collision_mask;
}

void MyriasEntityModel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_sprite_frames", "sprite_frames"), &MyriasEntityModel::set_sprite_frames);
    ClassDB::bind_method(D_METHOD("get_sprite_frames"), &MyriasEntityModel::get_sprite_frames);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"),
            "set_sprite_frames", "get_sprite_frames");

    ClassDB::bind_method(D_METHOD("set_radius", "radius"), &MyriasEntityModel::set_radius);
    ClassDB::bind_method(D_METHOD("get_radius"), &MyriasEntityModel::get_radius);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.0,1024.0,0.5"),
            "set_radius", "get_radius");

    ClassDB::bind_method(D_METHOD("set_collision_layer", "collision_layer"), &MyriasEntityModel::set_collision_layer);
    ClassDB::bind_method(D_METHOD("get_collision_layer"), &MyriasEntityModel::get_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_2D_PHYSICS),
            "set_collision_layer", "get_collision_layer");

    ClassDB::bind_method(D_METHOD("set_collision_mask", "collision_mask"), &MyriasEntityModel::set_collision_mask);
    ClassDB::bind_method(D_METHOD("get_collision_mask"), &MyriasEntityModel::get_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS),
            "set_collision_mask", "get_collision_mask");
}