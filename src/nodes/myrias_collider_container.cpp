#include "nodes/myrias_collider_container.h"

#include <godot_cpp/classes/collision_object2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/callable.hpp>

#include "server/myrias_server.h"

void MyriasColliderContainer::_register_child_node(Node *p_child) {
    if (p_child == nullptr || Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    CollisionObject2D *co = Object::cast_to<CollisionObject2D>(p_child);
    if (co == nullptr) {
        return;
    }
    const ObjectID oid(co->get_instance_id());
    if (m_child_colliders.has(oid)) {
        return;
    }
    MyriasServer *server = MyriasServer::get_singleton();
    if (server == nullptr) {
        return;
    }
    const int cid = server->register_collider(co, (int)m_mode);
    if (cid > 0) {
        m_child_colliders.insert(oid, cid);
    }
}

void MyriasColliderContainer::_unregister_child_node(Node *p_child) {
    if (p_child == nullptr) {
        return;
    }
    const ObjectID oid(p_child->get_instance_id());
    HashMap<ObjectID, int>::Iterator it = m_child_colliders.find(oid);
    if (it != m_child_colliders.end()) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr) {
            server->unregister_collider(it->value);
        }
        m_child_colliders.remove(it);
    }
}

void MyriasColliderContainer::_unregister_all() {
    MyriasServer *server = MyriasServer::get_singleton();
    if (server != nullptr) {
        for (const KeyValue<ObjectID, int> &kv : m_child_colliders) {
            server->unregister_collider(kv.value);
        }
    }
    m_child_colliders.clear();
}

void MyriasColliderContainer::_on_child_entered_tree(Node *p_child) {
    _register_child_node(p_child);
}

void MyriasColliderContainer::_on_child_exiting_tree(Node *p_child) {
    _unregister_child_node(p_child);
}

void MyriasColliderContainer::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: {
            if (is_node_ready()) {
                const int count = get_child_count();
                for (int i = 0; i < count; i++) {
                    _register_child_node(get_child(i));
                }
            }
            break;
        }
        case NOTIFICATION_READY: {
            const Callable entered_cb(this, "_on_child_entered_tree");
            if (!is_connected("child_entered_tree", entered_cb)) {
                connect("child_entered_tree", entered_cb);
            }
            const Callable exiting_cb(this, "_on_child_exiting_tree");
            if (!is_connected("child_exiting_tree", exiting_cb)) {
                connect("child_exiting_tree", exiting_cb);
            }
            const int count = get_child_count();
            for (int i = 0; i < count; i++) {
                _register_child_node(get_child(i));
            }
            break;
        }
        case NOTIFICATION_EXIT_TREE: {
            _unregister_all();
            break;
        }
        default:
            break;
    }
}

void MyriasColliderContainer::set_mode(MyriasCollider::Mode p_mode) {
    m_mode = p_mode;
    MyriasServer *server = MyriasServer::get_singleton();
    if (server != nullptr) {
        for (const KeyValue<ObjectID, int> &kv : m_child_colliders) {
            server->set_collider_mode(kv.value, (int)m_mode);
        }
    }
}

MyriasCollider::Mode MyriasColliderContainer::get_mode() const {
    return m_mode;
}

int MyriasColliderContainer::get_registered_collider_count() const {
    return (int)m_child_colliders.size();
}

void MyriasColliderContainer::rebuild_all() {
    const int count = get_child_count();
    for (int i = 0; i < count; i++) {
        _register_child_node(get_child(i));
    }
    MyriasServer *server = MyriasServer::get_singleton();
    if (server != nullptr) {
        for (const KeyValue<ObjectID, int> &kv : m_child_colliders) {
            server->rebuild_collider_shapes(kv.value);
        }
    }
}

void MyriasColliderContainer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mode", "mode"), &MyriasColliderContainer::set_mode);
    ClassDB::bind_method(D_METHOD("get_mode"), &MyriasColliderContainer::get_mode);
    ClassDB::bind_method(D_METHOD("get_registered_collider_count"), &MyriasColliderContainer::get_registered_collider_count);
    ClassDB::bind_method(D_METHOD("rebuild_all"), &MyriasColliderContainer::rebuild_all);
    ClassDB::bind_method(D_METHOD("_on_child_entered_tree", "child"), &MyriasColliderContainer::_on_child_entered_tree);
    ClassDB::bind_method(D_METHOD("_on_child_exiting_tree", "child"), &MyriasColliderContainer::_on_child_exiting_tree);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Auto,Static,Dynamic"), "set_mode", "get_mode");
}
