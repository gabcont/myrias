#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

class MyriasEntityModel : public Resource {
    GDCLASS(MyriasEntityModel, Resource);

private:
    Ref<SpriteFrames> m_sprite_frames;
    float m_radius = 8.0f;
    uint16_t m_collision_layer = 1;
    uint16_t m_collision_mask = 1;
    uint32_t m_traits = 0;

protected:
    static void _bind_methods();

public:
    void set_sprite_frames(const Ref<SpriteFrames> &p_frames);
    Ref<SpriteFrames> get_sprite_frames() const;

    void set_radius(float p_radius);
    float get_radius() const;

    void set_collision_layer(uint32_t p_layer);
    uint32_t get_collision_layer() const;

    void set_collision_mask(uint32_t p_mask);
    uint32_t get_collision_mask() const;

    void set_traits(uint32_t p_traits);
    uint32_t get_traits() const;
};
