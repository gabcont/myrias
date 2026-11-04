#include "nodes/myrias_collider.h"

#include <godot_cpp/classes/collision_object2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/static_body2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "server/myrias_server.h"

void MyriasCollider::_try_register() {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    MyriasServer *server = MyriasServer::get_singleton();
    if (server == nullptr) {
        return;
    }
    CollisionObject2D *parent = Object::cast_to<CollisionObject2D>(get_parent());
    if (parent == nullptr) {
        return;
    }
    if (m_collider_id > 0) {
        server->rebuild_collider_shapes(m_collider_id);
        return;
    }
    m_collider_id = server->register_collider(parent, (int)m_mode);
}

void MyriasCollider::_unregister() {
    if (m_collider_id > 0) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr) {
            server->unregister_collider(m_collider_id);
        }
        m_collider_id = -1;
    }
}

void MyriasCollider::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: {
            if (is_node_ready()) {
                _try_register();
            }
            break;
        }
        case NOTIFICATION_READY: {
            _try_register();
            break;
        }
        case NOTIFICATION_EXIT_TREE:
        case NOTIFICATION_UNPARENTED: {
            _unregister();
            break;
        }
        default:
            break;
    }
}

void MyriasCollider::set_mode(Mode p_mode) {
    m_mode = p_mode;
    if (m_collider_id > 0) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr) {
            server->set_collider_mode(m_collider_id, (int)m_mode);
        }
    }
}

MyriasCollider::Mode MyriasCollider::get_mode() const {
    return m_mode;
}

int MyriasCollider::get_collider_id() const {
    return m_collider_id;
}

bool MyriasCollider::is_resolved_static() const {
    if (m_collider_id > 0) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr) {
            return server->is_collider_static(m_collider_id);
        }
    }
    if (m_mode == MODE_STATIC) {
        return true;
    }
    if (m_mode == MODE_DYNAMIC) {
        return false;
    }
    return Object::cast_to<StaticBody2D>(get_parent()) != nullptr;
}

void MyriasCollider::rebuild_shapes() {
    if (m_collider_id > 0) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr) {
            server->rebuild_collider_shapes(m_collider_id);
        }
    }
}

void MyriasCollider::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mode", "mode"), &MyriasCollider::set_mode);
    ClassDB::bind_method(D_METHOD("get_mode"), &MyriasCollider::get_mode);
    ClassDB::bind_method(D_METHOD("get_collider_id"), &MyriasCollider::get_collider_id);
    ClassDB::bind_method(D_METHOD("is_resolved_static"), &MyriasCollider::is_resolved_static);
    ClassDB::bind_method(D_METHOD("rebuild_shapes"), &MyriasCollider::rebuild_shapes);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Auto,Static,Dynamic"), "set_mode", "get_mode");

    BIND_ENUM_CONSTANT(MODE_AUTO);
    BIND_ENUM_CONSTANT(MODE_STATIC);
    BIND_ENUM_CONSTANT(MODE_DYNAMIC);
}
