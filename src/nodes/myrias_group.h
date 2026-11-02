#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "resources/myrias_entity_model.h"
#include "resources/myrias_trait.h"

using namespace godot;

class MyriasGroup : public Node2D {
    GDCLASS(MyriasGroup, Node2D);

private:
    Ref<MyriasEntityModel> m_model;
    TypedArray<MyriasTrait> m_traits;
    int m_pool_size = 1024;
    int m_group_id = -1;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    MyriasGroup() = default;
    ~MyriasGroup();

    void set_model(const Ref<MyriasEntityModel> &p_model);
    Ref<MyriasEntityModel> get_model() const;

    void set_traits(const TypedArray<MyriasTrait> &p_traits);
    TypedArray<MyriasTrait> get_traits() const;

    void set_pool_size(int p_size);
    int get_pool_size() const;

    int get_group_id() const;

    void _disconnect_trait_signals();
    void _on_trait_changed();
    void _apply_traits();
};

