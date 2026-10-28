#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "resources/myrias_entity_model.h"
#include "structs/myrias_command.h"
#include "structs/myrias_entity_streams.h"
#include "structs/myrias_sprite_table.h"

using namespace godot;

class MyriasServer : public Object {
    GDCLASS(MyriasServer, Object);

private:
    static MyriasServer *s_singleton;

    EntitySoA m_entities;
    Vector<GroupContext> m_groups_ctx;
    int m_next_group_id = 0;
    uint64_t m_next_spawn_seq = 0;

    Ref<ArrayMesh> m_quad;
    bool m_tick_connected = false;
    uint64_t m_last_process_usec = 0;
    Vector<MyriasCommand> m_commands;

    GroupContext *find_group(int p_group_id);
    void load_mm_mesh_if_unloaded();
    void free_group_render_resources(GroupContext &r_ctx);
    void connect_tick();

    void exec_create_group(int p_group_id, const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item);
    void exec_remove_group(int p_group_id);
    void exec_spawn(const MyriasCommand &p_cmd);
    void exec_despawn(int p_group_id, int p_local_index);
    void flush_commands();
    void on_process_frame();

    void compact(GroupContext &r_ctx);
    void render_group(GroupContext &r_ctx);


protected:
    static void _bind_methods();

public:
    static MyriasServer *get_singleton() { return s_singleton; }

    MyriasServer();
    ~MyriasServer();

    // All the calls below only enqueue a command. The queue is executed in order at the
    // start of the next process(), before compaction and rendering.

    // Reserves a group id right away and queues the group creation.
    int create_group(const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item = RID());
    // Queues the group removal.
    void remove_group(int p_group_id);

    // Queues an entity appended at the end of the group's active range.
    void spawn(int p_group_id, Vector2 p_pos, Vector2 p_vel, int p_animation = 0, float p_scale = 1.0f, float p_rot = 0.0f);
    // Queues an entity removal by local index.
    void despawn(int p_group_id, int p_local_index);

    // Active entities in the given group, or in all groups if p_group_id is -1 (default).
    int get_active_count(int p_group_id = -1) const;

    // Per render frame (SceneTree.process_frame): commands, compaction, simple kinematics,
    // logic and render buffers.
    void process_tick(double p_delta);

    void refresh_group_canvas_item(int p_group_id, RID p_canvas_item);
};
