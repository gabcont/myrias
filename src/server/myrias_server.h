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
#include "physics/myrias_physics_space.h"
#include "physics/myrias_spatial_grid.h"
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

static inline bool is_valid_single_behavior_trait(uint32_t p_flag) {
    return p_flag != 0 && (p_flag & (p_flag - 1)) == 0 && (p_flag & TRAIT_BEHAVIOR_MASK) == p_flag;
}

static inline void kill_local(GroupContext &g, uint32_t p_local_idx) {
    uint64_t *bits = g.alive_bits.ptrw();
    if (ALIVE_TEST(bits, p_local_idx)) {
        ALIVE_CLEAR(bits, p_local_idx);
        g.active_amount--;
        float *o = g.buffer.ptrw() + (int64_t)p_local_idx * INSTANCE_STRIDE;
        o[0] = 0.0f;
        o[1] = 0.0f;
        o[4] = 0.0f;
        o[5] = 0.0f;
    }
}

static inline void tail_trim_bitset(GroupContext &g) {
    if (g.last_active == 0 || g.active_amount == 0) {
        g.last_active = 0;
        return;
    }
    const uint64_t *bits = g.alive_bits.ptr();
    int32_t w = (int32_t)((g.last_active - 1) >> 6);
    const uint32_t rem = g.last_active & 63;
    uint64_t word = bits[w];
    if (rem != 0) {
        word &= (1ull << rem) - 1ull;
    }
    while (word == 0) {
        w--;
        if (w < 0) {
            g.last_active = 0;
            return;
        }
        word = bits[w];
    }
    const uint32_t highest_bit = 63 - __builtin_clzll(word);
    g.last_active = (uint16_t)((w << 6) + highest_bit + 1);
}

class MyriasServer : public Object {
    GDCLASS(MyriasServer, Object);

private:
    static MyriasServer *s_singleton;

    EntitySoA m_entities;
    Vector<GroupContext> m_groups_ctx;
    MyriasSpatialGrid m_spatial_grid;
    MyriasPhysicsSpace m_physics_space;
    int m_next_group_id = 0;

    bool m_debug_draw_static_sdf = false;
    bool m_debug_draw_dynamic_colliders = false;
    bool m_debug_settings_initialized = false;

    Ref<ArrayMesh> m_quad;
    bool m_tick_connected = false;
    uint64_t m_last_process_usec = 0;
    Vector<MyriasCommand> m_commands;
    Vector<MyriasPhysicsEvent> m_physics_events;

    GroupContext *find_group(int p_group_id);
    void load_mm_mesh_if_unloaded();
    void init_group_render_resources(GroupContext &r_ctx);
    void free_group_render_resources(GroupContext &r_ctx);
    void connect_tick();
    RID get_root_canvas_rid() const;
    void update_physics_debug_draw();
    void step_group_bounds(GroupContext &r_group);
    void step_group_animation(GroupContext &r_group);
    const PhysicsTraitConfig *get_group_physics_config(uint16_t p_group_id) const;
    void step_world_physics(float p_dt, int p_total_proxies);

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

    // SoA fixed-capacity contiguous buffer pooling controls
    bool set_soa_capacity(int p_max_entities);
    int get_soa_capacity() const;
    int get_soa_used() const;

    // Configures the bounded spatial grid bounds and cell size.
    void configure_spatial_grid(Rect2 p_bounds, float p_cell_size = 32.0f);
    // Queries if any entity matching p_collision_mask (and in p_group_id if >= 0) overlaps the circle (p_pos, p_radius).
    bool query_collision(Vector2 p_pos, float p_radius, int p_group_id = -1, uint16_t p_collision_mask = 0xFFFF) const;

    // Physics Space (Static SDF & Dynamic Colliders)
    int register_collider(CollisionObject2D *p_node, int p_mode = COLLIDER_MODE_AUTO);
    void unregister_collider(int p_collider_id);
    bool set_collider_mode(int p_collider_id, int p_mode);
    bool rebuild_collider_shapes(int p_collider_id);
    void configure_static_sdf(Rect2 p_bounds, float p_cell_size = 8.0f);
    float sample_static_sdf_distance(Vector2 p_pos, uint16_t p_mask = 0xFFFF) const;
    Vector2 sample_static_sdf_normal(Vector2 p_pos) const;
    int sample_static_sdf_layer(Vector2 p_pos) const;
    int get_static_collider_count() const;
    int get_dynamic_collider_count() const;
    bool is_collider_static(int p_collider_id) const;
    Rect2 get_collider_aabb(int p_collider_id) const;
    void apply_collider_impulse(int p_collider_id, Vector2 p_impulse, Vector2 p_world_pos = Vector2());
    float get_collider_mass(int p_collider_id) const;
    float get_collider_inv_mass(int p_collider_id) const;
    float get_collider_inertia(int p_collider_id) const;
    float get_collider_inv_inertia(int p_collider_id) const;
    float get_collider_bounce(int p_collider_id) const;
    float get_collider_friction(int p_collider_id) const;
    Vector2 get_collider_linear_velocity(int p_collider_id) const;
    float get_collider_angular_velocity(int p_collider_id) const;
    Vector2 get_collider_point_velocity(int p_collider_id, Vector2 p_world_point) const;
    Dictionary get_area_space_override_info(int p_collider_id) const;
    float combine_bounce(float p_a, float p_b) const;
    float combine_friction(float p_a, float p_b) const;

    // Debug Visualization
    void set_debug_draw_static_sdf(bool p_enabled);
    bool is_debug_draw_static_sdf_enabled() const;
    void set_debug_draw_dynamic_colliders(bool p_enabled);
    bool is_debug_draw_dynamic_colliders_enabled() const;

    MyriasPhysicsSpace &get_physics_space() { return m_physics_space; }
    const MyriasPhysicsSpace &get_physics_space() const { return m_physics_space; }

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
