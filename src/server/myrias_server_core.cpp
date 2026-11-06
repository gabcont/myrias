#include "myrias_server.h"

#include "simd/myrias_kinematics.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "../profiling/myrias_profiling.h"

using namespace godot;

MyriasServer *MyriasServer::s_singleton = nullptr;

MyriasServer::MyriasServer() {
    s_singleton = this;
    set_soa_capacity(DEFAULT_SOA_CAPACITY);
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (ps != nullptr) {
        if (ps->has_setting("myrias/debug/draw_static_sdf")) {
            m_debug_draw_static_sdf = (bool)ps->get_setting("myrias/debug/draw_static_sdf", false);
        }
        if (ps->has_setting("myrias/debug/draw_dynamic_colliders")) {
            m_debug_draw_dynamic_colliders = (bool)ps->get_setting("myrias/debug/draw_dynamic_colliders", false);
        }
    }
    UtilityFunctions::print("[Myrias] Google Highway SIMD active. Dispatched target: ", myrias::get_hwy_target_name());
}

MyriasServer::~MyriasServer() {
    m_physics_space.cleanup_render_resources();
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs != nullptr) {
        for (int i = 0; i < m_groups_ctx.size(); i++) {
            free_group_render_resources(m_groups_ctx.ptrw()[i]);
        }
    }
    if (s_singleton == this) {
        s_singleton = nullptr;
    }
}

String MyriasServer::get_simd_target_name() const {
    return String(myrias::get_hwy_target_name());
}

GroupContext *MyriasServer::find_group(int p_group_id) {
    if (p_group_id < 0 || p_group_id > 65535) {
        return nullptr;
    }
    const uint16_t gid = (uint16_t)p_group_id;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if (m_groups_ctx[i].group_id == gid) {
            return &m_groups_ctx.ptrw()[i];
        }
    }
    return nullptr;
}

bool MyriasServer::set_soa_capacity(int p_max_entities) {
    if (p_max_entities < 0) {
        return false;
    }
    if (!m_entities.set_capacity((uint32_t)p_max_entities)) {
        return false;
    }
    m_spatial_grid.ensure_capacity(p_max_entities);
    return true;
}

int MyriasServer::get_soa_capacity() const {
    return (int)m_entities.m_capacity;
}

int MyriasServer::get_soa_used() const {
    return (int)m_entities.m_used;
}

void MyriasServer::connect_tick() {
    if (!m_debug_settings_initialized) {
        ProjectSettings *ps = ProjectSettings::get_singleton();
        if (ps != nullptr) {
            if (ps->has_setting("myrias/debug/draw_static_sdf")) {
                m_debug_draw_static_sdf = (bool)ps->get_setting("myrias/debug/draw_static_sdf", false);
            }
            if (ps->has_setting("myrias/debug/draw_dynamic_colliders")) {
                m_debug_draw_dynamic_colliders = (bool)ps->get_setting("myrias/debug/draw_dynamic_colliders", false);
            }
            m_debug_settings_initialized = true;
        }
    }
    if (m_tick_connected) {
        return;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr) {
        tree->connect("physics_frame", Callable(this, "on_physics_frame"));
        tree->connect("process_frame", Callable(this, "on_process_frame"));
        m_tick_connected = true;
    }
}

void MyriasServer::on_physics_frame() {
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr && tree->is_paused()) {
        return;
    }
    Engine *engine = Engine::get_singleton();
    const int tps = MAX(engine->get_physics_ticks_per_second(), 1);
    const double physics_delta = (1.0 / (double)tps) * engine->get_time_scale();
    physics_tick(physics_delta);
}

void MyriasServer::on_process_frame() {
    const uint64_t now = Time::get_singleton()->get_ticks_usec();
    const double delta = m_last_process_usec == 0 ? 0.0 : (now - m_last_process_usec) / 1000000.0;
    m_last_process_usec = now;

    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr && tree->is_paused()) {
        return;
    }
    process_tick(delta * Engine::get_singleton()->get_time_scale());
}

void MyriasServer::flush_commands() {
    MYRIAS_PROFILE_ZONE("Myrias::flush_commands");
    if (m_commands.is_empty()) {
        return;
    }
    const Vector<MyriasCommand> batch = m_commands;
    m_commands.clear();

    HashSet<uint16_t> created;
    HashSet<uint16_t> doomed;
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (c.type == MyriasCommand::CREATE_GROUP) {
            created.insert(c.group_id);
        } else if (c.type == MyriasCommand::REMOVE_GROUP && created.has(c.group_id)) {
            doomed.insert(c.group_id);
        }
    }

    // Phase 1: CREATE_GROUP
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::CREATE_GROUP) {
            exec_create_group(c);
        }
    }

    // Phase 1.5: CONFIGURE_TRAIT / REMOVE_TRAIT
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id)) {
            if (c.type == MyriasCommand::CONFIGURE_TRAIT) {
                exec_configure_trait(c);
            } else if (c.type == MyriasCommand::REMOVE_TRAIT) {
                exec_remove_trait(c);
            }
        }
    }

    // Phase 2: DESPAWN
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::DESPAWN) {
            exec_despawn(c.group_id, c.local_index);
        }
    }

    // Phase 3: SPAWN
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::SPAWN) {
            exec_spawn(c);
        }
    }

    // Phase 3.5: SET_ENTITY_TRAIT
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::SET_ENTITY_TRAIT) {
            exec_set_entity_trait(c);
        }
    }

    // Phase 4: REMOVE_GROUP
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::REMOVE_GROUP) {
            exec_remove_group(c.group_id);
        }
    }
}

void MyriasServer::push_physics_event(const MyriasPhysicsEvent &p_event) {
    m_physics_events.push_back(p_event);
}

void MyriasServer::emit_animation_change_event(int p_group_id, int p_local_index, int p_animation) {
    MyriasPhysicsEvent ev;
    ev.type = MyriasPhysicsEvent::CHANGE_ANIMATION;
    ev.group_id = (uint16_t)p_group_id;
    ev.local_idx = (uint16_t)p_local_index;
    ev.param = p_animation;
    m_physics_events.push_back(ev);
}

void MyriasServer::emit_despawn_event(int p_group_id, int p_local_index) {
    MyriasPhysicsEvent ev;
    ev.type = MyriasPhysicsEvent::DESPAWN;
    ev.group_id = (uint16_t)p_group_id;
    ev.local_idx = (uint16_t)p_local_index;
    m_physics_events.push_back(ev);
}

void MyriasServer::flush_physics_events() {
    MYRIAS_PROFILE_ZONE("Myrias::flush_physics_events");
    if (m_physics_events.is_empty()) {
        return;
    }
    const Vector<MyriasPhysicsEvent> events = m_physics_events;
    m_physics_events.clear();

    uint16_t *anim_base = m_entities.cur_animation.ptrw();
    uint16_t *frame_base = m_entities.cur_frame.ptrw();
    float *lifetime_base = m_entities.lifetime.ptrw();
    const float *scale_base = m_entities.scale.ptr();
    const float *rot_base = m_entities.rot.ptr();

    for (int i = 0; i < events.size(); i++) {
        const MyriasPhysicsEvent &ev = events[i];
        GroupContext *g = find_group(ev.group_id);
        if (g == nullptr || (uint32_t)ev.local_idx >= g->last_active) {
            continue;
        }
        const uint32_t abs_idx = g->offset + ev.local_idx;
        switch (ev.type) {
            case MyriasPhysicsEvent::CHANGE_ANIMATION:
                if (ALIVE_TEST(g->alive_bits.ptr(), ev.local_idx)) {
                    const uint16_t anim = (uint16_t)MAX(ev.param, 0);
                    anim_base[abs_idx] = anim;
                    frame_base[abs_idx] = 0;
                    lifetime_base[abs_idx] = 0.0f; // Reset animation playback time.
                    update_render_packet(*g, ev.local_idx, anim, 0, scale_base[abs_idx], rot_base[abs_idx]);
                }
                break;
            case MyriasPhysicsEvent::DESPAWN:
                kill_local(*g, ev.local_idx);
                finalize_group_deaths(*g);
                break;
            case MyriasPhysicsEvent::COLLISION:
                break;
        }
    }
}

void MyriasServer::physics_tick(double p_physics_delta) {
    MYRIAS_PROFILE_ZONE("Myrias::physics_tick");

    flush_physics_events();
    flush_commands();

    m_physics_space.update();
    if (m_debug_draw_static_sdf || m_debug_draw_dynamic_colliders) {
        update_physics_debug_draw();
    }

    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];

        if (GROUP_IS_DIRTY(g.traits)) {
            m_entities.compact_group(g.offset, g.alive_bits.ptrw(), g.buffer.ptrw(), g.trait_bits, g.active_amount, g.last_active);
            GROUP_CLEAR_DIRTY(g.traits);
        }

        // Bounds Process: bounce on X, kill on Y
        step_group_bounds(g);

        // Advance entity lifetimes before traits and animation
        {
            const uint64_t *bits = g.alive_bits.ptr();
            const uint32_t num_words = (g.last_active + 63) / 64;
            float *lifetime = m_entities.lifetime.ptrw() + g.offset;
            const float dt = (float)p_physics_delta;
            for (uint32_t w = 0; w < num_words; w++) {
                uint64_t word = bits[w];
                if (w == num_words - 1 && (g.last_active & 63) != 0) {
                    word &= (1ull << (g.last_active & 63)) - 1ull;
                }
                while (word) {
                    const uint32_t j = (w << 6) + __builtin_ctzll(word);
                    word &= word - 1;
                    lifetime[j] += dt;
                }
            }
        }

        // Behavior & Visual Traits Process
        if ((g.traits & TRAIT_LOGIC_MASK) != 0) {
            MYRIAS_PROFILE_ZONE("Myrias::traits");
            myrias::run_group_traits(g, m_entities, (float)p_physics_delta);
        }
    }

    // Spatial Grid Build & Physics Resolution
    const int total_proxies = m_spatial_grid.build(m_entities, m_groups_ctx);
    step_world_physics((float)p_physics_delta, total_proxies);
}

void MyriasServer::process_tick(double p_delta) {
    MYRIAS_PROFILE_ZONE("Myrias::process_tick");

    flush_physics_events();
    flush_commands();

    // 1. Compaction, Kinematics, and Animation Logic
    int total_active = 0;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];

        if (GROUP_IS_DIRTY(g.traits)) {
            m_entities.compact_group(g.offset, g.alive_bits.ptrw(), g.buffer.ptrw(), g.trait_bits, g.active_amount, g.last_active);
            GROUP_CLEAR_DIRTY(g.traits);
        }
        total_active += g.active_amount;

        const uint32_t n = g.last_active;

        // Kinematics Process: pure integration, branchless, SIMD-ready
        {
            MYRIAS_PROFILE_ZONE("Myrias::kinematics");
            const float dt = (float)p_delta;
            myrias::run_kinematics(
                m_entities.phys_x.ptrw() + g.offset,
                m_entities.phys_y.ptrw() + g.offset,
                m_entities.phys_vel_x.ptr() + g.offset,
                m_entities.phys_vel_y.ptr() + g.offset,
                n,
                dt
            );
        }

        // Animation Process: derive animation frame from aged lifetime
        step_group_animation(g);
    }

    // 2. Render Groups
    {
        MYRIAS_PROFILE_ZONE("Myrias::render_group");
        for (int i = 0; i < m_groups_ctx.size(); i++) {
            render_group(i);
        }
    }

    MYRIAS_PROFILE_PLOT("Active Entities", total_active);
    MYRIAS_PROFILE_FRAME();
}

void MyriasServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_group", "model", "pool_size", "canvas_item"), &MyriasServer::create_group, DEFVAL(RID()));
    ClassDB::bind_method(D_METHOD("remove_group", "group_id"), &MyriasServer::remove_group);
    ClassDB::bind_method(D_METHOD("refresh_group_canvas_item", "group_id", "canvas_item"), &MyriasServer::refresh_group_canvas_item);
    ClassDB::bind_method(D_METHOD("spawn", "group_id", "pos", "vel", "animation", "scale", "rot"),
            &MyriasServer::spawn, DEFVAL(0), DEFVAL(1.0f), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("despawn", "group_id", "local_index"), &MyriasServer::despawn);

    ClassDB::bind_method(D_METHOD("configure_trait", "group_id", "trait", "owner"),
            &MyriasServer::configure_trait_resource, DEFVAL(nullptr));
    ClassDB::bind_method(D_METHOD("remove_group_trait", "group_id", "trait_flag"), &MyriasServer::remove_group_trait);
    ClassDB::bind_method(D_METHOD("set_entity_trait", "group_id", "local_index", "trait_flag", "enabled"), &MyriasServer::set_entity_trait);
    ClassDB::bind_method(D_METHOD("has_entity_trait", "group_id", "local_index", "trait_flag"), &MyriasServer::has_entity_trait);
    ClassDB::bind_method(D_METHOD("get_group_traits", "group_id"), &MyriasServer::get_group_traits);

    ClassDB::bind_method(D_METHOD("get_entity_position", "group_id", "local_index"), &MyriasServer::get_entity_position);
    ClassDB::bind_method(D_METHOD("get_entity_velocity", "group_id", "local_index"), &MyriasServer::get_entity_velocity);
    ClassDB::bind_method(D_METHOD("get_entity_scale", "group_id", "local_index"), &MyriasServer::get_entity_scale);
    ClassDB::bind_method(D_METHOD("get_entity_rotation", "group_id", "local_index"), &MyriasServer::get_entity_rotation);

    ClassDB::bind_method(D_METHOD("get_active_count", "group_id"), &MyriasServer::get_active_count, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("set_soa_capacity", "max_entities"), &MyriasServer::set_soa_capacity);
    ClassDB::bind_method(D_METHOD("get_soa_capacity"), &MyriasServer::get_soa_capacity);
    ClassDB::bind_method(D_METHOD("get_soa_used"), &MyriasServer::get_soa_used);
    ClassDB::bind_method(D_METHOD("get_simd_target_name"), &MyriasServer::get_simd_target_name);
    ClassDB::bind_method(D_METHOD("configure_spatial_grid", "bounds", "cell_size"), &MyriasServer::configure_spatial_grid, DEFVAL(32.0f));
    ClassDB::bind_method(D_METHOD("query_collision", "pos", "radius", "group_id", "collision_mask"), &MyriasServer::query_collision, DEFVAL(-1), DEFVAL(0xFFFF));

    // Physics Space & Debug Draw bindings
    ClassDB::bind_method(D_METHOD("register_collider", "node", "mode"), &MyriasServer::register_collider, DEFVAL(COLLIDER_MODE_AUTO));
    ClassDB::bind_method(D_METHOD("unregister_collider", "collider_id"), &MyriasServer::unregister_collider);
    ClassDB::bind_method(D_METHOD("set_collider_mode", "collider_id", "mode"), &MyriasServer::set_collider_mode);
    ClassDB::bind_method(D_METHOD("rebuild_collider_shapes", "collider_id"), &MyriasServer::rebuild_collider_shapes);
    ClassDB::bind_method(D_METHOD("configure_static_sdf", "bounds", "cell_size"), &MyriasServer::configure_static_sdf, DEFVAL(8.0f));
    ClassDB::bind_method(D_METHOD("sample_static_sdf_distance", "pos", "mask"), &MyriasServer::sample_static_sdf_distance, DEFVAL(0xFFFF));
    ClassDB::bind_method(D_METHOD("sample_static_sdf_normal", "pos"), &MyriasServer::sample_static_sdf_normal);
    ClassDB::bind_method(D_METHOD("sample_static_sdf_layer", "pos"), &MyriasServer::sample_static_sdf_layer);
    ClassDB::bind_method(D_METHOD("get_static_collider_count"), &MyriasServer::get_static_collider_count);
    ClassDB::bind_method(D_METHOD("get_dynamic_collider_count"), &MyriasServer::get_dynamic_collider_count);
    ClassDB::bind_method(D_METHOD("is_collider_static", "collider_id"), &MyriasServer::is_collider_static);
    ClassDB::bind_method(D_METHOD("get_collider_aabb", "collider_id"), &MyriasServer::get_collider_aabb);
    ClassDB::bind_method(D_METHOD("apply_collider_impulse", "collider_id", "impulse", "world_pos"), &MyriasServer::apply_collider_impulse, DEFVAL(Vector2()));
    ClassDB::bind_method(D_METHOD("get_collider_mass", "collider_id"), &MyriasServer::get_collider_mass);
    ClassDB::bind_method(D_METHOD("get_collider_inv_mass", "collider_id"), &MyriasServer::get_collider_inv_mass);
    ClassDB::bind_method(D_METHOD("get_collider_inertia", "collider_id"), &MyriasServer::get_collider_inertia);
    ClassDB::bind_method(D_METHOD("get_collider_inv_inertia", "collider_id"), &MyriasServer::get_collider_inv_inertia);
    ClassDB::bind_method(D_METHOD("get_collider_bounce", "collider_id"), &MyriasServer::get_collider_bounce);
    ClassDB::bind_method(D_METHOD("get_collider_friction", "collider_id"), &MyriasServer::get_collider_friction);
    ClassDB::bind_method(D_METHOD("get_collider_linear_velocity", "collider_id"), &MyriasServer::get_collider_linear_velocity);
    ClassDB::bind_method(D_METHOD("get_collider_angular_velocity", "collider_id"), &MyriasServer::get_collider_angular_velocity);
    ClassDB::bind_method(D_METHOD("get_collider_point_velocity", "collider_id", "world_point"), &MyriasServer::get_collider_point_velocity);
    ClassDB::bind_method(D_METHOD("get_area_space_override_info", "collider_id"), &MyriasServer::get_area_space_override_info);
    ClassDB::bind_method(D_METHOD("combine_bounce", "a", "b"), &MyriasServer::combine_bounce);
    ClassDB::bind_method(D_METHOD("combine_friction", "a", "b"), &MyriasServer::combine_friction);
    ClassDB::bind_method(D_METHOD("set_debug_draw_static_sdf", "enabled"), &MyriasServer::set_debug_draw_static_sdf);
    ClassDB::bind_method(D_METHOD("is_debug_draw_static_sdf_enabled"), &MyriasServer::is_debug_draw_static_sdf_enabled);
    ClassDB::bind_method(D_METHOD("set_debug_draw_dynamic_colliders", "enabled"), &MyriasServer::set_debug_draw_dynamic_colliders);
    ClassDB::bind_method(D_METHOD("is_debug_draw_dynamic_colliders_enabled"), &MyriasServer::is_debug_draw_dynamic_colliders_enabled);

    ClassDB::bind_method(D_METHOD("emit_animation_change_event", "group_id", "local_index", "animation"), &MyriasServer::emit_animation_change_event);
    ClassDB::bind_method(D_METHOD("emit_despawn_event", "group_id", "local_index"), &MyriasServer::emit_despawn_event);
    ClassDB::bind_method(D_METHOD("on_physics_frame"), &MyriasServer::on_physics_frame);
    ClassDB::bind_method(D_METHOD("on_process_frame"), &MyriasServer::on_process_frame);

    BIND_CONSTANT(TRAIT_FOLLOW_STEERING);
    BIND_CONSTANT(TRAIT_FOLLOW_ANGULAR);
    BIND_CONSTANT(TRAIT_SINE_WAVE);
    BIND_CONSTANT(TRAIT_CURVE_MODIFIER);
    BIND_CONSTANT(TRAIT_FACE_VELOCITY);
    BIND_CONSTANT(TRAIT_FACE_NODE);
    BIND_CONSTANT(TRAIT_PHYSICS);

    BIND_CONSTANT(CURVE_TARGET_SCALE);
    BIND_CONSTANT(CURVE_TARGET_ROTATION);
    BIND_CONSTANT(CURVE_TARGET_SPEED);

    BIND_CONSTANT(COLLIDER_MODE_AUTO);
    BIND_CONSTANT(COLLIDER_MODE_STATIC);
    BIND_CONSTANT(COLLIDER_MODE_DYNAMIC);
}
