#include "myrias_server.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world2d.hpp>

#include "../profiling/myrias_profiling.h"

using namespace godot;

void MyriasServer::step_group_bounds(GroupContext &r_group) {
    MYRIAS_PROFILE_ZONE("Myrias::bounds");
    const uint32_t n = r_group.last_active;
    const float *px = m_entities.phys_x.ptr() + r_group.offset;
    const float *py = m_entities.phys_y.ptr() + r_group.offset;
    float *pvx = m_entities.phys_vel_x.ptrw() + r_group.offset;
    for (uint32_t j = 0; j < n; j++) {
        if (px[j] <= 0.0f || px[j] >= 1280.0f) {
            pvx[j] = -pvx[j];
        }
        if (py[j] <= 0.0f || py[j] >= 720.0f) {
            kill_local(r_group, j);
        }
    }
    finalize_group_deaths(r_group);
}

const PhysicsTraitConfig *MyriasServer::get_group_physics_config(uint16_t p_group_id) const {
    static const PhysicsTraitConfig s_default_cfg;
    constexpr uint32_t s_phys = 14 - TRAIT_BEHAVIOR_FIRST_BIT;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if (m_groups_ctx[i].group_id == p_group_id) {
            const TraitConfig *base_cfg = m_groups_ctx[i].trait_configs[s_phys].get();
            if (base_cfg != nullptr) {
                return static_cast<const PhysicsTraitConfig *>(base_cfg);
            }
            break;
        }
    }
    return &s_default_cfg;
}

void MyriasServer::step_world_physics(float p_dt, int p_total_proxies) {
    if (p_total_proxies <= 0) {
        return;
    }
    bool any_phys_group = false;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if ((m_groups_ctx[i].traits & TRAIT_PHYSICS) != 0) {
            any_phys_group = true;
            break;
        }
    }
    if (!any_phys_group) {
        return;
    }

    MYRIAS_PROFILE_ZONE("Myrias::step_world_physics");

    const int cols = m_spatial_grid.get_cols();
    const int *starts = m_spatial_grid.get_cell_start();
    const int *counts = m_spatial_grid.get_cell_count();
    PhysicsProxy *proxies = m_spatial_grid.get_sorted_proxies().ptrw();

    // Pass A: Area2D Space Modifiers (Static & Dynamic Area2Ds)
    auto process_area_list = [&](const Vector<MyriasColliderEntry> &p_colliders) {
        for (int c_idx = 0; c_idx < p_colliders.size(); c_idx++) {
            const MyriasColliderEntry &entry = p_colliders[c_idx];
            if (!entry.is_area) {
                continue;
            }
            const bool has_grav = entry.gravity_override_mode != PhysicsServer2D::AREA_SPACE_OVERRIDE_DISABLED;
            const bool has_damp = entry.linear_damp_override_mode != PhysicsServer2D::AREA_SPACE_OVERRIDE_DISABLED && entry.area_linear_damp > 0.0f;
            if (!has_grav && !has_damp) {
                continue;
            }

            for (int s_idx = 0; s_idx < entry.shapes.size(); s_idx++) {
                const MyriasColliderShape &shape = entry.shapes[s_idx];
                int min_cx = 0, max_cx = 0, min_cy = 0, max_cy = 0;
                m_spatial_grid.get_cell_range(shape.world_aabb, min_cx, max_cx, min_cy, max_cy);

                for (int cy = min_cy; cy <= max_cy; cy++) {
                    const int row_off = cy * cols;
                    for (int cx = min_cx; cx <= max_cx; cx++) {
                        const int cell = row_off + cx;
                        const int start = starts[cell];
                        const int end = start + counts[cell];
                        for (int i = start; i < end; i++) {
                            PhysicsProxy &px = proxies[i];
                            if ((px.traits & TRAIT_PHYSICS) == 0) {
                                continue;
                            }
                            if (((px.collision_mask & entry.collision_layer) == 0) && ((entry.collision_mask & px.collision_layer) == 0)) {
                                continue;
                            }
                            const Vector2 pos(px.x, px.y);
                            if (shape.eval_sdf(pos) <= px.radius) {
                                const PhysicsTraitConfig *cfg = get_group_physics_config(px.group_id);
                                if (has_grav) {
                                    Vector2 g_vec;
                                    if (entry.gravity_is_point) {
                                        const Vector2 pt_center = entry.world_transform.xform(entry.gravity_vector);
                                        const Vector2 to_center = pt_center - pos;
                                        const float dist = to_center.length();
                                        const Vector2 dir = dist > 1e-5f ? (to_center / dist) : Vector2();
                                        float mag = entry.gravity;
                                        if (entry.gravity_point_unit_distance > 0.0f) {
                                            const float d_clamped = MAX(dist, entry.gravity_point_unit_distance * 0.001f);
                                            const float ratio = entry.gravity_point_unit_distance / d_clamped;
                                            mag *= ratio * ratio;
                                        }
                                        g_vec = dir * mag;
                                    } else {
                                        g_vec = entry.gravity_vector.normalized() * entry.gravity;
                                    }
                                    px.vel_x += g_vec.x * cfg->gravity_scale * p_dt;
                                    px.vel_y += g_vec.y * cfg->gravity_scale * p_dt;
                                }
                                if (has_damp) {
                                    const float damp_factor = MAX(1.0f - entry.area_linear_damp * p_dt, 0.0f);
                                    px.vel_x *= damp_factor;
                                    px.vel_y *= damp_factor;
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    process_area_list(m_physics_space.get_static_colliders());
    process_area_list(m_physics_space.get_dynamic_colliders());

    // Pass B: Static SDF Collisions & Baseline Damping
    const bool has_static_colliders = m_physics_space.get_static_collider_count() > 0;
    for (int i = 0; i < p_total_proxies; i++) {
        PhysicsProxy &px = proxies[i];
        if ((px.traits & TRAIT_PHYSICS) == 0) {
            continue;
        }
        const PhysicsTraitConfig *cfg = get_group_physics_config(px.group_id);
        if (cfg->linear_damp > 0.0f) {
            const float damp = MAX(1.0f - cfg->linear_damp * p_dt, 0.0f);
            px.vel_x *= damp;
            px.vel_y *= damp;
        }
        if (has_static_colliders) {
            const Vector2 pos(px.x, px.y);
            const float d = m_physics_space.sample_sdf_distance(pos, px.collision_mask);
            if (d < px.radius) {
                const Vector2 n = m_physics_space.sample_sdf_normal(pos);
                const float pen = px.radius - d;
                px.x += n.x * pen;
                px.y += n.y * pen;
                const Vector2 contact_pos = Vector2(px.x, px.y) - n * px.radius;
                const uint16_t cid = m_physics_space.sample_sdf_collider_id(pos);
                const MyriasColliderEntry *sc = m_physics_space.get_static_collider_by_id(cid);
                const float col_bounce = sc != nullptr ? sc->bounce : 0.0f;
                const float col_friction = sc != nullptr ? sc->friction : 1.0f;
                const Vector2 v_surf = sc != nullptr ? sc->get_velocity_at_world_point(contact_pos) : Vector2();
                const Vector2 v_rel(px.vel_x - v_surf.x, px.vel_y - v_surf.y);
                const float vn = v_rel.dot(n);
                if (vn < 0.0f) {
                    const float e = MyriasPhysicsSpace::combine_bounce(cfg->bounce, col_bounce);
                    const float mu = MyriasPhysicsSpace::combine_friction(cfg->friction, col_friction);
                    const float jn = -(1.0f + e) * vn;
                    const Vector2 t(-n.y, n.x);
                    const float vt = v_rel.dot(t);
                    const float jt = CLAMP(-vt, -mu * jn, mu * jn);
                    px.vel_x += jn * n.x + jt * t.x;
                    px.vel_y += jn * n.y + jt * t.y;
                }
            }
        }
    }

    // Pass C: Dynamic Body Collisions & Two-Way Impulses
    const Vector<MyriasColliderEntry> &dyn_colliders = m_physics_space.get_dynamic_colliders();
    for (int c_idx = 0; c_idx < dyn_colliders.size(); c_idx++) {
        const MyriasColliderEntry &entry = dyn_colliders[c_idx];
        if (entry.is_area) {
            continue;
        }
        for (int s_idx = 0; s_idx < entry.shapes.size(); s_idx++) {
            const MyriasColliderShape &shape = entry.shapes[s_idx];
            int min_cx = 0, max_cx = 0, min_cy = 0, max_cy = 0;
            m_spatial_grid.get_cell_range(shape.world_aabb, min_cx, max_cx, min_cy, max_cy);

            for (int cy = min_cy; cy <= max_cy; cy++) {
                const int row_off = cy * cols;
                for (int cx = min_cx; cx <= max_cx; cx++) {
                    const int cell = row_off + cx;
                    const int start = starts[cell];
                    const int end = start + counts[cell];
                    for (int i = start; i < end; i++) {
                        PhysicsProxy &px = proxies[i];
                        if ((px.traits & TRAIT_PHYSICS) == 0) {
                            continue;
                        }
                        if (((px.collision_mask & entry.collision_layer) == 0) && ((entry.collision_mask & px.collision_layer) == 0)) {
                            continue;
                        }
                        const Vector2 pos(px.x, px.y);
                        const float d = shape.eval_sdf(pos);
                        if (d < px.radius) {
                            const Vector2 n = shape.eval_normal(pos);
                            const float pen = px.radius - d;
                            px.x += n.x * pen;
                            px.y += n.y * pen;
                            const Vector2 contact_pos = Vector2(px.x, px.y) - n * px.radius;
                            const PhysicsTraitConfig *cfg = get_group_physics_config(px.group_id);
                            const Vector2 r_b = contact_pos - entry.get_world_center_of_mass();
                            const Vector2 v_b = entry.get_velocity_at_world_point(contact_pos);
                            const Vector2 v_rel(px.vel_x - v_b.x, px.vel_y - v_b.y);
                            const float vn = v_rel.dot(n);
                            if (vn < 0.0f) {
                                const float e = MyriasPhysicsSpace::combine_bounce(cfg->bounce, entry.bounce);
                                const float mu = MyriasPhysicsSpace::combine_friction(cfg->friction, entry.friction);
                                const float rn = r_b.cross(n);
                                const float k_n = MAX(cfg->inv_mass + entry.inv_mass + rn * rn * entry.inv_inertia, 1e-6f);
                                const float jn = -(1.0f + e) * vn / k_n;
                                const Vector2 t(-n.y, n.x);
                                const float vt = v_rel.dot(t);
                                const float rt = r_b.cross(t);
                                const float k_t = MAX(cfg->inv_mass + entry.inv_mass + rt * rt * entry.inv_inertia, 1e-6f);
                                const float jt = CLAMP(-vt / k_t, -mu * jn, mu * jn);
                                const Vector2 impulse = n * jn + t * jt;
                                px.vel_x += impulse.x * cfg->inv_mass;
                                px.vel_y += impulse.y * cfg->inv_mass;
                                if (entry.inv_mass > 0.0f) {
                                    m_physics_space.apply_collider_impulse(entry.collider_id, -impulse, contact_pos);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass D: Sync modified positions & velocities back to SoA
    m_spatial_grid.sync_to_soa(m_entities, m_groups_ctx, p_total_proxies);
}

void MyriasServer::configure_spatial_grid(Rect2 p_bounds, float p_cell_size) {
    m_spatial_grid.configure(p_bounds, p_cell_size);
}

bool MyriasServer::query_collision(Vector2 p_pos, float p_radius, int p_group_id, uint16_t p_collision_mask) const {
    return m_spatial_grid.query_collision(p_pos, p_radius, p_group_id, p_collision_mask);
}

RID MyriasServer::get_root_canvas_rid() const {
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree == nullptr) {
        return RID();
    }
    Window *root = tree->get_root();
    if (root == nullptr) {
        return RID();
    }
    Ref<World2D> world = root->find_world_2d();
    if (world.is_null()) {
        return RID();
    }
    return world->get_canvas();
}

void MyriasServer::update_physics_debug_draw() {
    m_physics_space.update_debug_draw(get_root_canvas_rid(), m_debug_draw_static_sdf, m_debug_draw_dynamic_colliders);
}

int MyriasServer::register_collider(CollisionObject2D *p_node, int p_mode) {
    connect_tick();
    const MyriasColliderMode mode = (MyriasColliderMode)CLAMP(p_mode, (int)COLLIDER_MODE_AUTO, (int)COLLIDER_MODE_DYNAMIC);
    return m_physics_space.register_collision_object(p_node, mode);
}

void MyriasServer::unregister_collider(int p_collider_id) {
    m_physics_space.unregister_collider(p_collider_id);
}

bool MyriasServer::set_collider_mode(int p_collider_id, int p_mode) {
    const MyriasColliderMode mode = (MyriasColliderMode)CLAMP(p_mode, (int)COLLIDER_MODE_AUTO, (int)COLLIDER_MODE_DYNAMIC);
    return m_physics_space.set_collider_mode(p_collider_id, mode);
}

bool MyriasServer::rebuild_collider_shapes(int p_collider_id) {
    return m_physics_space.rebuild_collider_shapes(p_collider_id);
}

void MyriasServer::configure_static_sdf(Rect2 p_bounds, float p_cell_size) {
    connect_tick();
    m_physics_space.configure_sdf(p_bounds, p_cell_size);
}

float MyriasServer::sample_static_sdf_distance(Vector2 p_pos, uint16_t p_mask) const {
    return m_physics_space.sample_sdf_distance(p_pos, p_mask);
}

Vector2 MyriasServer::sample_static_sdf_normal(Vector2 p_pos) const {
    return m_physics_space.sample_sdf_normal(p_pos);
}

int MyriasServer::sample_static_sdf_layer(Vector2 p_pos) const {
    return (int)m_physics_space.sample_sdf_layer(p_pos);
}

int MyriasServer::get_static_collider_count() const {
    return m_physics_space.get_static_collider_count();
}

int MyriasServer::get_dynamic_collider_count() const {
    return m_physics_space.get_dynamic_collider_count();
}

bool MyriasServer::is_collider_static(int p_collider_id) const {
    return m_physics_space.is_collider_static(p_collider_id);
}

Rect2 MyriasServer::get_collider_aabb(int p_collider_id) const {
    return m_physics_space.get_collider_aabb(p_collider_id);
}

void MyriasServer::apply_collider_impulse(int p_collider_id, Vector2 p_impulse, Vector2 p_world_pos) {
    m_physics_space.apply_collider_impulse(p_collider_id, p_impulse, p_world_pos);
}

float MyriasServer::get_collider_mass(int p_collider_id) const {
    return m_physics_space.get_collider_mass(p_collider_id);
}

float MyriasServer::get_collider_inv_mass(int p_collider_id) const {
    return m_physics_space.get_collider_inv_mass(p_collider_id);
}

float MyriasServer::get_collider_inertia(int p_collider_id) const {
    return m_physics_space.get_collider_inertia(p_collider_id);
}

float MyriasServer::get_collider_inv_inertia(int p_collider_id) const {
    return m_physics_space.get_collider_inv_inertia(p_collider_id);
}

float MyriasServer::get_collider_bounce(int p_collider_id) const {
    return m_physics_space.get_collider_bounce(p_collider_id);
}

float MyriasServer::get_collider_friction(int p_collider_id) const {
    return m_physics_space.get_collider_friction(p_collider_id);
}

Vector2 MyriasServer::get_collider_linear_velocity(int p_collider_id) const {
    return m_physics_space.get_collider_linear_velocity(p_collider_id);
}

float MyriasServer::get_collider_angular_velocity(int p_collider_id) const {
    return m_physics_space.get_collider_angular_velocity(p_collider_id);
}

Vector2 MyriasServer::get_collider_point_velocity(int p_collider_id, Vector2 p_world_point) const {
    return m_physics_space.get_collider_point_velocity(p_collider_id, p_world_point);
}

Dictionary MyriasServer::get_area_space_override_info(int p_collider_id) const {
    return m_physics_space.get_area_space_override_info(p_collider_id);
}

float MyriasServer::combine_bounce(float p_a, float p_b) const {
    return MyriasPhysicsSpace::combine_bounce(p_a, p_b);
}

float MyriasServer::combine_friction(float p_a, float p_b) const {
    return MyriasPhysicsSpace::combine_friction(p_a, p_b);
}

void MyriasServer::set_debug_draw_static_sdf(bool p_enabled) {
    m_debug_settings_initialized = true;
    m_debug_draw_static_sdf = p_enabled;
    connect_tick();
    update_physics_debug_draw();
}

bool MyriasServer::is_debug_draw_static_sdf_enabled() const {
    return m_debug_draw_static_sdf;
}

void MyriasServer::set_debug_draw_dynamic_colliders(bool p_enabled) {
    m_debug_settings_initialized = true;
    m_debug_draw_dynamic_colliders = p_enabled;
    connect_tick();
    update_physics_debug_draw();
}

bool MyriasServer::is_debug_draw_dynamic_colliders_enabled() const {
    return m_debug_draw_dynamic_colliders;
}
