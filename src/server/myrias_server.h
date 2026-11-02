#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "resources/myrias_entity_model.h"
#include "structs/myrias_command.h"
#include "structs/myrias_entity_streams.h"
#include "structs/myrias_physics_event.h"
#include "structs/myrias_spatial_grid.h"
#include "traits/myrias_traits.h"

// MultiMesh 2D layout without color but with custom data: 8 (transform) + 4 (custom).
static constexpr int INSTANCE_STRIDE = 12;

// Custom data holds the frame UV rect (x, y, w, h), remapped here onto the quad UVs.
static const char *MYRIAS_SHADER_CODE = R"(
shader_type canvas_item;
void vertex() {
    UV = INSTANCE_CUSTOM.xy + UV * INSTANCE_CUSTOM.zw;
}
)";

#include "resources/myrias_trait.h"

using namespace godot;

class MyriasServer : public Object {
    GDCLASS(MyriasServer, Object);

private:
    static MyriasServer *s_singleton;

    EntitySoA m_entities;
    Vector<GroupContext> m_groups_ctx;
    MyriasSpatialGrid m_spatial_grid;
    int m_next_group_id = 0;
    uint64_t m_next_spawn_seq = 0;

    Ref<ArrayMesh> m_quad;
    bool m_tick_connected = false;
    uint64_t m_last_process_usec = 0;
    Vector<MyriasCommand> m_commands;
    Vector<MyriasPhysicsEvent> m_physics_events;

    GroupContext *find_group(int p_group_id);
    void load_mm_mesh_if_unloaded();
    void free_group_render_resources(GroupContext &r_ctx);
    void connect_tick();

    void exec_create_group(const MyriasCommand &p_cmd);
    void exec_remove_group(int p_group_id);
    void exec_spawn(const MyriasCommand &p_cmd);
    void exec_despawn(int p_group_id, int p_local_index);
    void exec_configure_trait(const MyriasCommand &p_cmd);
    void exec_remove_trait(const MyriasCommand &p_cmd);
    void exec_set_entity_trait(const MyriasCommand &p_cmd);
    void flush_commands();
    void flush_physics_events();
    void on_physics_frame();
    void on_process_frame();

    void finalize_group_deaths(GroupContext &g);
    void update_render_packet(GroupContext &g, uint32_t idx, uint16_t anim_idx, uint16_t frame_idx, float scale, float rot);
    void render_group(int p_group_idx);


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

    // Unified trait configuration (enqueued to run in flush_commands)
    void configure_trait(int p_group_id, MyriasTraitType p_trait, const std::shared_ptr<TraitConfig> &p_config);
    void configure_trait_resource(int p_group_id, const Ref<MyriasTrait> &p_trait, Node2D *p_owner = nullptr);
    void remove_group_trait(int p_group_id, uint32_t p_trait_flag);
    void set_entity_trait(int p_group_id, int p_local_index, uint32_t p_trait_flag, bool p_enabled);

    // Queries
    bool has_entity_trait(int p_group_id, int p_local_index, uint32_t p_trait_flag) const;
    uint32_t get_group_traits(int p_group_id) const;

    // Entity inspection queries
    Vector2 get_entity_position(int p_group_id, int p_local_index) const;
    Vector2 get_entity_velocity(int p_group_id, int p_local_index) const;
    float get_entity_scale(int p_group_id, int p_local_index) const;
    float get_entity_rotation(int p_group_id, int p_local_index) const;

    String get_simd_target_name() const;
    void refresh_group_canvas_item(int p_group_id, RID p_canvas_item);

    // Active entities in the given group, or in all groups if p_group_id is -1 (default).
    int get_active_count(int p_group_id = -1) const;

    // Configures the bounded spatial grid bounds and cell size.
    void configure_spatial_grid(Rect2 p_bounds, float p_cell_size = 32.0f);
    // Queries if any entity matching p_collision_mask (and in p_group_id if >= 0) overlaps the circle (p_pos, p_radius).
    bool query_collision(Vector2 p_pos, float p_radius, int p_group_id = -1, uint16_t p_collision_mask = 0xFFFF) const;

    // Pushes a physics event into the event buffer to be processed at the start of the next frame.
    void push_physics_event(const MyriasPhysicsEvent &p_event);
    void emit_animation_change_event(int p_group_id, int p_local_index, int p_animation);
    void emit_despawn_event(int p_group_id, int p_local_index);

    // Per physics frame (SceneTree.physics_frame): commands, compaction, bounds, lifetime,
    // traits, and spatial grid build.
    void physics_tick(double p_physics_delta);

    // Per render frame (SceneTree.process_frame): commands, compaction, kinematics,
    // animation, and render buffers.
    void process_tick(double p_delta);
};
