#include "myrias_group.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "server/myrias_server.h"

void MyriasGroup::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_model", "model"), &MyriasGroup::set_model);
    ClassDB::bind_method(D_METHOD("get_model"), &MyriasGroup::get_model);
    ClassDB::bind_method(D_METHOD("set_pool_size", "pool_size"), &MyriasGroup::set_pool_size);
    ClassDB::bind_method(D_METHOD("get_pool_size"), &MyriasGroup::get_pool_size);
    ClassDB::bind_method(D_METHOD("get_group_id"), &MyriasGroup::get_group_id);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "model", PROPERTY_HINT_RESOURCE_TYPE, "MyriasEntityModel"),
            "set_model", "get_model");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "pool_size", PROPERTY_HINT_RANGE, "1,65535,1"),
            "set_pool_size", "get_pool_size");
}

void MyriasGroup::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    if (p_what == NOTIFICATION_EXIT_TREE) {
        MyriasServer *server = MyriasServer::get_singleton();
        if (server != nullptr && m_group_id >= 0) {
            server->remove_group(m_group_id);
        }
        // Drop the queued multimesh draw call now, while our CanvasItem is surely valid.
        RenderingServer::get_singleton()->canvas_item_clear(get_canvas_item());
        m_group_id = -1;
        return;
    }
    if (p_what != NOTIFICATION_ENTER_TREE) {
        return;
    }
    MyriasServer *server = MyriasServer::get_singleton();
    if (server == nullptr) {
        UtilityFunctions::push_error("MyriasGroup: MyriasServer is not available.");
        return;
    }
    if (m_model.is_null()) {
        UtilityFunctions::push_error("MyriasGroup: no model assigned, group not created.");
        return;
    }
    m_group_id = server->create_group(m_model, m_pool_size, get_canvas_item());
}

void MyriasGroup::set_model(const Ref<MyriasEntityModel> &p_model) {
    m_model = p_model;
}

Ref<MyriasEntityModel> MyriasGroup::get_model() const {
    return m_model;
}

void MyriasGroup::set_pool_size(int p_size) {
    m_pool_size = CLAMP(p_size, 1, 65535);
}

int MyriasGroup::get_pool_size() const {
    return m_pool_size;
}

int MyriasGroup::get_group_id() const {
    return m_group_id;
}
