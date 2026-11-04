#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include "nodes/myrias_collider.h"

using namespace godot;

class MyriasColliderContainer : public Node2D {
    GDCLASS(MyriasColliderContainer, Node2D);

private:
    MyriasCollider::Mode m_mode = MyriasCollider::MODE_AUTO;
    HashMap<ObjectID, int> m_child_colliders;

    void _register_child_node(Node *p_child);
    void _unregister_child_node(Node *p_child);
    void _unregister_all();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    MyriasColliderContainer() = default;
    ~MyriasColliderContainer() override = default;

    void set_mode(MyriasCollider::Mode p_mode);
    MyriasCollider::Mode get_mode() const;

    int get_registered_collider_count() const;
    void rebuild_all();

    void _on_child_entered_tree(Node *p_child);
    void _on_child_exiting_tree(Node *p_child);
};
