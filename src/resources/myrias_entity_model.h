#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

class MyriasEntityModel : public Resource {
    GDCLASS(MyriasEntityModel, Resource);

private:
    Ref<SpriteFrames> m_sprite_frames;

protected:
    static void _bind_methods();

public:
    void set_sprite_frames(const Ref<SpriteFrames> &p_frames);
    Ref<SpriteFrames> get_sprite_frames() const;
};
