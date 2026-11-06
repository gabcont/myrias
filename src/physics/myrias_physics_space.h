#pragma once

#include <cstdint>
#include <godot_cpp/classes/collision_object2d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

enum MyriasColliderMode : int {
    COLLIDER_MODE_AUTO = 0,
    COLLIDER_MODE_STATIC = 1,
    COLLIDER_MODE_DYNAMIC = 2,
};

enum MyriasShapeType : uint8_t {
    MYRIAS_SHAPE_CIRCLE = 0,
    MYRIAS_SHAPE_RECTANGLE = 1,
    MYRIAS_SHAPE_CAPSULE = 2,
    MYRIAS_SHAPE_SEGMENT = 3,
    MYRIAS_SHAPE_SEPARATION_RAY = 4,
    MYRIAS_SHAPE_WORLD_BOUNDARY = 5,
    MYRIAS_SHAPE_CONVEX_POLYGON = 6,
    MYRIAS_SHAPE_CONCAVE_POLYGON = 7,
};

struct alignas(8) MyriasSDFCell {
    float distance = 1e6f;           // Signed distance (< 0 inside, > 0 outside)
    uint16_t collision_layer = 0;    // 16-bit layer bitmask of the nearest static collider(s)
    uint16_t collider_id = 0xFFFF;   // Static collider ID (0xFFFF if empty)
};

static_assert(sizeof(MyriasSDFCell) == 8, "MyriasSDFCell must be exactly 8 bytes");

struct MyriasColliderShape {
    MyriasShapeType type = MYRIAS_SHAPE_CIRCLE;
    RID shape_rid;
    Transform2D local_transform;
    Vector2 local_extents; // .x = radius for circle/capsule, distance for boundary; (half_w, half_h) for rect; .y = half_mid_height for capsule
    Vector2 local_a;       // Segment endpoint A or WorldBoundary normal
    Vector2 local_b;       // Segment endpoint B or SeparationRay tip
    bool slide_on_slope = false;
    Vector<Vector2> local_points;

    // Cached world-space representation
    Vector2 world_pos;
    Vector2 world_axis_x = Vector2(1.0f, 0.0f);
    Vector2 world_axis_y = Vector2(0.0f, 1.0f);
    Vector2 world_extents;
    Vector2 world_a;
    Vector2 world_b;
    Vector<Vector2> world_points;
    Rect2 world_aabb;

    void update_world_cache(const Transform2D &p_body_transform) {
        const Transform2D t = p_body_transform * local_transform;
        world_pos = t.get_origin();
        const float sx = t.columns[0].length();
        const float sy = t.columns[1].length();
        world_axis_x = sx > 1e-6f ? (t.columns[0] / sx) : Vector2(1.0f, 0.0f);
        world_axis_y = sy > 1e-6f ? (t.columns[1] / sy) : Vector2(0.0f, 1.0f);

        switch (type) {
            case MYRIAS_SHAPE_CIRCLE: {
                const float r = local_extents.x * MAX(sx, sy);
                world_extents = Vector2(r, r);
                world_aabb = Rect2(world_pos - Vector2(r, r), Vector2(r * 2.0f, r * 2.0f));
                break;
            }
            case MYRIAS_SHAPE_RECTANGLE: {
                const float hx = local_extents.x * sx;
                const float hy = local_extents.y * sy;
                world_extents = Vector2(hx, hy);
                const float ex = Math::abs(world_axis_x.x) * hx + Math::abs(world_axis_y.x) * hy;
                const float ey = Math::abs(world_axis_x.y) * hx + Math::abs(world_axis_y.y) * hy;
                world_aabb = Rect2(world_pos - Vector2(ex, ey), Vector2(ex * 2.0f, ey * 2.0f));
                break;
            }
            case MYRIAS_SHAPE_CAPSULE: {
                const float r = local_extents.x * sx;
                const float hl = local_extents.y * sy;
                world_extents = Vector2(r, hl);
                world_a = world_pos - world_axis_y * hl;
                world_b = world_pos + world_axis_y * hl;
                const float min_x = MIN(world_a.x, world_b.x) - r;
                const float min_y = MIN(world_a.y, world_b.y) - r;
                const float max_x = MAX(world_a.x, world_b.x) + r;
                const float max_y = MAX(world_a.y, world_b.y) + r;
                world_aabb = Rect2(Vector2(min_x, min_y), Vector2(max_x - min_x, max_y - min_y));
                break;
            }
            case MYRIAS_SHAPE_SEGMENT:
            case MYRIAS_SHAPE_SEPARATION_RAY: {
                world_a = t.xform(local_a);
                world_b = t.xform(local_b);
                world_pos = (world_a + world_b) * 0.5f;
                const Vector2 seg = world_b - world_a;
                const float seg_len = seg.length();
                world_axis_y = seg_len > 1e-6f ? (seg / seg_len) : Vector2(0.0f, 1.0f);
                world_axis_x = Vector2(world_axis_y.y, -world_axis_y.x);
                world_extents = Vector2(0.0f, seg_len * 0.5f);
                const float min_x = MIN(world_a.x, world_b.x) - 1.0f;
                const float min_y = MIN(world_a.y, world_b.y) - 1.0f;
                const float max_x = MAX(world_a.x, world_b.x) + 1.0f;
                const float max_y = MAX(world_a.y, world_b.y) + 1.0f;
                world_aabb = Rect2(Vector2(min_x, min_y), Vector2(max_x - min_x, max_y - min_y));
                break;
            }
            case MYRIAS_SHAPE_WORLD_BOUNDARY: {
                const Vector2 p0_local = local_a * local_extents.x;
                world_pos = t.xform(p0_local);
                const Vector2 t_local(-local_a.y, local_a.x);
                const Vector2 t_world = t.basis_xform(t_local);
                const float t_len = t_world.length();
                world_axis_x = t_len > 1e-6f ? (t_world / t_len) : Vector2(1.0f, 0.0f);
                const Vector2 n_perp(world_axis_x.y, -world_axis_x.x);
                const Vector2 n_raw = t.basis_xform(local_a);
                world_axis_y = (n_perp.dot(n_raw) >= 0.0f) ? n_perp : -n_perp;
                world_aabb = Rect2(Vector2(-1e5f, -1e5f), Vector2(2e5f, 2e5f));
                break;
            }
            case MYRIAS_SHAPE_CONVEX_POLYGON:
            case MYRIAS_SHAPE_CONCAVE_POLYGON: {
                const int n = local_points.size();
                if (world_points.size() != n) {
                    world_points.resize(n);
                }
                if (n == 0) {
                    world_aabb = Rect2(world_pos, Vector2());
                } else {
                    const Vector2 *src = local_points.ptr();
                    Vector2 *dst = world_points.ptrw();
                    const Vector2 p0 = t.xform(src[0]);
                    dst[0] = p0;
                    float min_x = p0.x, max_x = p0.x;
                    float min_y = p0.y, max_y = p0.y;
                    Vector2 sum = p0;
                    for (int i = 1; i < n; i++) {
                        const Vector2 pw = t.xform(src[i]);
                        dst[i] = pw;
                        min_x = MIN(min_x, pw.x);
                        max_x = MAX(max_x, pw.x);
                        min_y = MIN(min_y, pw.y);
                        max_y = MAX(max_y, pw.y);
                        sum += pw;
                    }
                    world_pos = sum / (float)n;
                    world_aabb = Rect2(Vector2(min_x - 1.0f, min_y - 1.0f), Vector2((max_x - min_x) + 2.0f, (max_y - min_y) + 2.0f));
                }
                break;
            }
        }
    }

    float eval_sdf(Vector2 p_point) const {
        switch (type) {
            case MYRIAS_SHAPE_CIRCLE:
                return p_point.distance_to(world_pos) - world_extents.x;
            case MYRIAS_SHAPE_RECTANGLE: {
                const Vector2 d = p_point - world_pos;
                const float qx = Math::abs(d.dot(world_axis_x)) - world_extents.x;
                const float qy = Math::abs(d.dot(world_axis_y)) - world_extents.y;
                const float out_x = MAX(qx, 0.0f);
                const float out_y = MAX(qy, 0.0f);
                return Math::sqrt(out_x * out_x + out_y * out_y) + MIN(MAX(qx, qy), 0.0f);
            }
            case MYRIAS_SHAPE_CAPSULE: {
                const Vector2 d = p_point - world_pos;
                const float proj = CLAMP(d.dot(world_axis_y), -world_extents.y, world_extents.y);
                const Vector2 closest = world_pos + world_axis_y * proj;
                return p_point.distance_to(closest) - world_extents.x;
            }
            case MYRIAS_SHAPE_SEGMENT:
            case MYRIAS_SHAPE_SEPARATION_RAY: {
                const Vector2 e = world_b - world_a;
                const Vector2 w = p_point - world_a;
                const float ee = e.length_squared();
                const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                const Vector2 closest = world_a + e * h;
                return p_point.distance_to(closest);
            }
            case MYRIAS_SHAPE_WORLD_BOUNDARY:
                return (p_point - world_pos).dot(world_axis_y);
            case MYRIAS_SHAPE_CONVEX_POLYGON: {
                const int n = world_points.size();
                if (n < 3) {
                    return 1e6f;
                }
                const Vector2 *pts = world_points.ptr();
                float d2 = (p_point - pts[0]).length_squared();
                float s = 1.0f;
                for (int i = 0, j = n - 1; i < n; j = i, i++) {
                    const Vector2 e = pts[j] - pts[i];
                    const Vector2 w = p_point - pts[i];
                    const float ee = e.length_squared();
                    const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                    const Vector2 b = w - e * h;
                    d2 = MIN(d2, b.length_squared());
                    const bool c0 = p_point.y >= pts[i].y;
                    const bool c1 = p_point.y < pts[j].y;
                    const bool c2 = (e.x * w.y) > (e.y * w.x);
                    if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) {
                        s = -s;
                    }
                }
                return s * Math::sqrt(d2);
            }
            case MYRIAS_SHAPE_CONCAVE_POLYGON: {
                const int n = world_points.size();
                if (n < 2) {
                    return 1e6f;
                }
                const Vector2 *pts = world_points.ptr();
                float best_d2 = 1e30f;
                for (int k = 0; k + 1 < n; k += 2) {
                    const Vector2 a = pts[k];
                    const Vector2 e = pts[k + 1] - a;
                    const Vector2 w = p_point - a;
                    const float ee = e.length_squared();
                    const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                    const float bd2 = (w - e * h).length_squared();
                    if (bd2 < best_d2) {
                        best_d2 = bd2;
                    }
                }
                return Math::sqrt(best_d2);
            }
        }
        return 1e6f;
    }

    Vector2 eval_normal(Vector2 p_point) const {
        switch (type) {
            case MYRIAS_SHAPE_CIRCLE: {
                const Vector2 d = p_point - world_pos;
                const float len = d.length();
                return len > 1e-6f ? (d / len) : Vector2(0.0f, -1.0f);
            }
            case MYRIAS_SHAPE_RECTANGLE: {
                const Vector2 d = p_point - world_pos;
                const float lx = d.dot(world_axis_x);
                const float ly = d.dot(world_axis_y);
                const float qx = Math::abs(lx) - world_extents.x;
                const float qy = Math::abs(ly) - world_extents.y;
                if (qx > 0.0f || qy > 0.0f) {
                    const float ox = MAX(qx, 0.0f) * (lx >= 0.0f ? 1.0f : -1.0f);
                    const float oy = MAX(qy, 0.0f) * (ly >= 0.0f ? 1.0f : -1.0f);
                    const Vector2 world_out = world_axis_x * ox + world_axis_y * oy;
                    const float len = world_out.length();
                    if (len > 1e-6f) {
                        return world_out / len;
                    }
                }
                if (qx > qy) {
                    return lx >= 0.0f ? world_axis_x : -world_axis_x;
                }
                return ly >= 0.0f ? world_axis_y : -world_axis_y;
            }
            case MYRIAS_SHAPE_CAPSULE: {
                const Vector2 d = p_point - world_pos;
                const float proj = CLAMP(d.dot(world_axis_y), -world_extents.y, world_extents.y);
                const Vector2 closest = world_pos + world_axis_y * proj;
                const Vector2 diff = p_point - closest;
                const float len = diff.length();
                return len > 1e-6f ? (diff / len) : world_axis_x;
            }
            case MYRIAS_SHAPE_SEPARATION_RAY:
                return world_axis_y;
            case MYRIAS_SHAPE_SEGMENT: {
                const Vector2 e = world_b - world_a;
                const Vector2 w = p_point - world_a;
                const float ee = e.length_squared();
                const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                const Vector2 closest = world_a + e * h;
                const Vector2 diff = p_point - closest;
                const float len = diff.length();
                return len > 1e-6f ? (diff / len) : world_axis_x;
            }
            case MYRIAS_SHAPE_WORLD_BOUNDARY:
                return world_axis_y;
            case MYRIAS_SHAPE_CONVEX_POLYGON: {
                const int n = world_points.size();
                if (n < 3) {
                    return Vector2(0.0f, -1.0f);
                }
                const Vector2 *pts = world_points.ptr();
                float best_d2 = 1e30f;
                Vector2 best_b;
                Vector2 best_edge(1.0f, 0.0f);
                float s = 1.0f;
                for (int i = 0, j = n - 1; i < n; j = i, i++) {
                    const Vector2 e = pts[j] - pts[i];
                    const Vector2 w = p_point - pts[i];
                    const float ee = e.length_squared();
                    const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                    const Vector2 b = w - e * h;
                    const float bd2 = b.length_squared();
                    if (bd2 < best_d2) {
                        best_d2 = bd2;
                        best_b = b;
                        best_edge = e;
                    }
                    const bool c0 = p_point.y >= pts[i].y;
                    const bool c1 = p_point.y < pts[j].y;
                    const bool c2 = (e.x * w.y) > (e.y * w.x);
                    if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) {
                        s = -s;
                    }
                }
                const float blen = best_b.length();
                if (blen > 1e-6f) {
                    return (best_b / blen) * s;
                }
                Vector2 perp(best_edge.y, -best_edge.x);
                if (perp.dot(pts[0] - world_pos) < 0.0f) {
                    perp = -perp;
                }
                const float plen = perp.length();
                return plen > 1e-6f ? (perp / plen) : Vector2(0.0f, -1.0f);
            }
            case MYRIAS_SHAPE_CONCAVE_POLYGON: {
                const int n = world_points.size();
                if (n < 2) {
                    return Vector2(0.0f, -1.0f);
                }
                const Vector2 *pts = world_points.ptr();
                float best_d2 = 1e30f;
                Vector2 best_diff;
                Vector2 best_edge(1.0f, 0.0f);
                for (int k = 0; k + 1 < n; k += 2) {
                    const Vector2 a = pts[k];
                    const Vector2 e = pts[k + 1] - a;
                    const Vector2 w = p_point - a;
                    const float ee = e.length_squared();
                    const float h = ee > 1e-8f ? CLAMP(w.dot(e) / ee, 0.0f, 1.0f) : 0.0f;
                    const Vector2 diff = w - e * h;
                    const float bd2 = diff.length_squared();
                    if (bd2 < best_d2) {
                        best_d2 = bd2;
                        best_diff = diff;
                        best_edge = e;
                    }
                }
                const float len = best_diff.length();
                if (len > 1e-6f) {
                    return best_diff / len;
                }
                const Vector2 perp(best_edge.y, -best_edge.x);
                const float plen = perp.length();
                return plen > 1e-6f ? (perp / plen) : Vector2(0.0f, -1.0f);
            }
        }
        return Vector2(0.0f, -1.0f);
    }
};

struct MyriasColliderEntry {
    uint16_t collider_id = 0;
    ObjectID node_id;
    RID rid;
    bool is_area = false;
    uint16_t collision_layer = 1;
    uint16_t collision_mask = 1;
    Transform2D world_transform;
    Rect2 combined_aabb;
    Vector<MyriasColliderShape> shapes;

    // Body Physics Metadata (!is_area)
    PhysicsServer2D::BodyMode body_mode = PhysicsServer2D::BODY_MODE_STATIC;
    float bounce = 0.0f;   // Signed (< 0 indicates absorbent PhysicsMaterial)
    float friction = 1.0f; // Signed (< 0 indicates rough PhysicsMaterial)
    float mass = 1.0f;
    float inv_mass = 0.0f;
    float inertia = 0.0f;
    float inv_inertia = 0.0f;
    Vector2 center_of_mass;
    Vector2 linear_velocity;
    float angular_velocity = 0.0f;
    float gravity_scale = 1.0f;
    PhysicsServer2D::BodyDampMode linear_damp_mode = PhysicsServer2D::BODY_DAMP_MODE_COMBINE;
    PhysicsServer2D::BodyDampMode angular_damp_mode = PhysicsServer2D::BODY_DAMP_MODE_COMBINE;
    float linear_damp = 0.0f;
    float angular_damp = 0.0f;

    // Area2D Physics Metadata (is_area)
    bool monitoring = true;
    bool monitorable = true;
    int priority = 0;
    PhysicsServer2D::AreaSpaceOverrideMode gravity_override_mode = PhysicsServer2D::AREA_SPACE_OVERRIDE_DISABLED;
    float gravity = 980.0f;
    Vector2 gravity_vector = Vector2(0.0f, 1.0f);
    bool gravity_is_point = false;
    float gravity_point_unit_distance = 0.0f;
    PhysicsServer2D::AreaSpaceOverrideMode linear_damp_override_mode = PhysicsServer2D::AREA_SPACE_OVERRIDE_DISABLED;
    float area_linear_damp = 0.1f;
    PhysicsServer2D::AreaSpaceOverrideMode angular_damp_override_mode = PhysicsServer2D::AREA_SPACE_OVERRIDE_DISABLED;
    float area_angular_damp = 1.0f;

    Vector2 get_world_center_of_mass() const {
        return world_transform.xform(center_of_mass);
    }

    Vector2 get_velocity_at_world_point(Vector2 p_world_point) const {
        const Vector2 r = p_world_point - get_world_center_of_mass();
        return linear_velocity + Vector2(-angular_velocity * r.y, angular_velocity * r.x);
    }
};

class MyriasPhysicsSpace {
private:
    Vector<MyriasColliderEntry> m_static_colliders;
    Vector<MyriasColliderEntry> m_dynamic_colliders;
    uint16_t m_next_collider_id = 1;

    // Static SDF Grid
    Rect2 m_sdf_bounds = Rect2(0.0f, 0.0f, 2048.0f, 1152.0f);
    float m_cell_size = 8.0f;
    float m_inv_cell_size = 1.0f / 8.0f;
    int m_cols = 256;
    int m_rows = 144;
    bool m_sdf_dirty = true;
    bool m_sdf_texture_dirty = true;
    Vector<MyriasSDFCell> m_sdf_cells;

    // Debug Visualization
    RID m_debug_ci;
    RID m_debug_parent_canvas;
    Ref<ImageTexture> m_debug_sdf_tex;
    bool m_debug_was_drawn = false;

    static bool resolve_is_static(CollisionObject2D *p_node, MyriasColliderMode p_mode);
    static void extract_shapes_from_node(CollisionObject2D *p_node, MyriasColliderEntry &r_entry);
    static void extract_physics_params_from_node(CollisionObject2D *p_node, MyriasColliderEntry &r_entry);
    static void update_entry_transform_and_aabb(CollisionObject2D *p_node, MyriasColliderEntry &r_entry);

    MyriasColliderEntry *find_collider_mut(int p_collider_id, bool *r_is_in_static = nullptr, int *r_index = nullptr);
    const MyriasColliderEntry *find_collider(int p_collider_id) const;
    uint16_t allocate_collider_id();

public:
    MyriasPhysicsSpace();
    ~MyriasPhysicsSpace();

    static float combine_bounce(float p_a, float p_b) {
        return CLAMP(p_a + p_b, 0.0f, 1.0f);
    }

    static float combine_friction(float p_a, float p_b) {
        if (p_a < 0.0f || p_b < 0.0f) {
            return Math::abs(MIN(p_a, p_b));
        }
        return p_a * p_b;
    }

    void configure_sdf(const Rect2 &p_bounds, float p_cell_size = 8.0f);
    Rect2 get_sdf_bounds() const { return m_sdf_bounds; }
    float get_sdf_cell_size() const { return m_cell_size; }

    int register_collision_object(CollisionObject2D *p_node, MyriasColliderMode p_mode = COLLIDER_MODE_AUTO);
    void unregister_collider(int p_collider_id);
    bool set_collider_mode(int p_collider_id, MyriasColliderMode p_mode);
    bool rebuild_collider_shapes(int p_collider_id);

    void update();
    void rebuild_static_sdf();

    float sample_sdf_distance(Vector2 p_pos, uint16_t p_mask = 0xFFFF) const;
    Vector2 sample_sdf_normal(Vector2 p_pos) const;
    uint16_t sample_sdf_layer(Vector2 p_pos) const;
    uint16_t sample_sdf_collider_id(Vector2 p_pos) const;
    const MyriasColliderEntry *get_static_collider_by_id(uint16_t p_collider_id) const;

    int get_static_collider_count() const { return m_static_colliders.size(); }
    int get_dynamic_collider_count() const { return m_dynamic_colliders.size(); }
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

    const Vector<MyriasColliderEntry> &get_static_colliders() const { return m_static_colliders; }
    const Vector<MyriasColliderEntry> &get_dynamic_colliders() const { return m_dynamic_colliders; }
    const Vector<MyriasSDFCell> &get_sdf_cells() const { return m_sdf_cells; }

    void update_debug_draw(RID p_parent_canvas, bool p_draw_sdf, bool p_draw_dynamic);
    void cleanup_render_resources();
};
