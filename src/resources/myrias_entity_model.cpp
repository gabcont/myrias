#include "myrias_entity_model.h"

void MyriasEntityModel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_sprite_frames", "sprite_frames"), &MyriasEntityModel::set_sprite_frames);
    ClassDB::bind_method(D_METHOD("get_sprite_frames"), &MyriasEntityModel::get_sprite_frames);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"),
            "set_sprite_frames", "get_sprite_frames");
}

void MyriasEntityModel::set_sprite_frames(const Ref<SpriteFrames> &p_frames) {
    m_sprite_frames = p_frames;
}

Ref<SpriteFrames> MyriasEntityModel::get_sprite_frames() const {
    return m_sprite_frames;
}
