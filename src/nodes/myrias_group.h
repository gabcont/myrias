#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "resources/myrias_entity_model.h"

using namespace godot;

class MyriasGroup : public Node2D {
    GDCLASS(MyriasGroup, Node2D);

private:
    Ref<MyriasEntityModel> m_model;
    int m_pool_size = 1024;
    int m_group_id = -1;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void set_model(const Ref<MyriasEntityModel> &p_model);
    Ref<MyriasEntityModel> get_model() const;

    void set_pool_size(int p_size);
    int get_pool_size() const;

    int get_group_id() const;
};
