#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

class MyriasCollider : public Node {
    GDCLASS(MyriasCollider, Node);

public:
    enum Mode {
        MODE_AUTO = 0,
        MODE_STATIC = 1,
        MODE_DYNAMIC = 2,
    };

private:
    Mode m_mode = MODE_AUTO;
    int m_collider_id = -1;

    void _try_register();
    void _unregister();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    MyriasCollider() = default;
    ~MyriasCollider() override = default;

    void set_mode(Mode p_mode);
    Mode get_mode() const;

    int get_collider_id() const;
    bool is_resolved_static() const;
    void rebuild_shapes();
};

VARIANT_ENUM_CAST(MyriasCollider::Mode);
