#pragma once

#include <cstdint>
#include <godot_cpp/classes/collision_object2d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/templates/vector.hpp>
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
    Vector2 local_extents; // .x = radius for circle; (half_w, half_h) for rectangle

    // Cached world-space representation
    Vector2 world_pos;
    Vector2 world_axis_x = Vector2(1.0f, 0.0f);
    Vector2 world_axis_y = Vector2(0.0f, 1.0f);
    Vector2 world_extents;
    Rect2 world_aabb;

    void update_world_cache(const Transform2D &p_body_transform) {
        const Transform2D t = p_body_transform * local_transform;
        world_pos = t.get_origin();
        const float sx = t.columns[0].length();
        const float sy = t.columns[1].length();
        world_axis_x = sx > 1e-6f ? (t.columns[0] / sx) : Vector2(1.0f, 0.0f);
        world_axis_y = sy > 1e-6f ? (t.columns[1] / sy) : Vector2(0.0f, 1.0f);

        if (type == MYRIAS_SHAPE_CIRCLE) {
            const float r = local_extents.x * MAX(sx, sy);
            world_extents = Vector2(r, r);
            world_aabb = Rect2(world_pos - Vector2(r, r), Vector2(r * 2.0f, r * 2.0f));
        } else {
            const float hx = local_extents.x * sx;
            const float hy = local_extents.y * sy;
            world_extents = Vector2(hx, hy);
            const float ex = Math::abs(world_axis_x.x) * hx + Math::abs(world_axis_y.x) * hy;
            const float ey = Math::abs(world_axis_x.y) * hx + Math::abs(world_axis_y.y) * hy;
            world_aabb = Rect2(world_pos - Vector2(ex, ey), Vector2(ex * 2.0f, ey * 2.0f));
        }
    }

    float eval_sdf(Vector2 p_point) const {
        if (type == MYRIAS_SHAPE_CIRCLE) {
            return p_point.distance_to(world_pos) - world_extents.x;
        }
        const Vector2 d = p_point - world_pos;
        const float qx = Math::abs(d.dot(world_axis_x)) - world_extents.x;
        const float qy = Math::abs(d.dot(world_axis_y)) - world_extents.y;
        const float out_x = MAX(qx, 0.0f);
        const float out_y = MAX(qy, 0.0f);
        return Math::sqrt(out_x * out_x + out_y * out_y) + MIN(MAX(qx, qy), 0.0f);
    }
};

struct MyriasColliderEntry {
    uint16_t collider_id = 0;
    ObjectID node_id;
    RID rid;
    bool is_area = false;
    MyriasColliderMode mode = COLLIDER_MODE_AUTO;
    bool resolved_static = false;
    uint16_t collision_layer = 1;
    uint16_t collision_mask = 1;
    Transform2D world_transform;
    Rect2 combined_aabb;
    Vector<MyriasColliderShape> shapes;
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
    static void update_entry_transform_and_aabb(CollisionObject2D *p_node, MyriasColliderEntry &r_entry);

    MyriasColliderEntry *find_collider_mut(int p_collider_id, bool *r_is_in_static = nullptr, int *r_index = nullptr);
    const MyriasColliderEntry *find_collider(int p_collider_id) const;
    uint16_t allocate_collider_id();

public:
    MyriasPhysicsSpace();
    ~MyriasPhysicsSpace();

    void configure_sdf(const Rect2 &p_bounds, float p_cell_size = 8.0f);
    Rect2 get_sdf_bounds() const { return m_sdf_bounds; }
    float get_sdf_cell_size() const { return m_cell_size; }

    int register_collision_object(CollisionObject2D *p_node, MyriasColliderMode p_mode = COLLIDER_MODE_AUTO);
    void unregister_collider(int p_collider_id);
    bool set_collider_mode(int p_collider_id, MyriasColliderMode p_mode);
    int get_collider_mode(int p_collider_id) const;
    bool rebuild_collider_shapes(int p_collider_id);

    void update();
    void rebuild_static_sdf();

    float sample_sdf_distance(Vector2 p_pos, uint16_t p_mask = 0xFFFF) const;
    Vector2 sample_sdf_normal(Vector2 p_pos) const;
    uint16_t sample_sdf_layer(Vector2 p_pos) const;

    int get_static_collider_count() const { return m_static_colliders.size(); }
    int get_dynamic_collider_count() const { return m_dynamic_colliders.size(); }
    bool is_collider_static(int p_collider_id) const;
    Rect2 get_collider_aabb(int p_collider_id) const;

    const Vector<MyriasColliderEntry> &get_static_colliders() const { return m_static_colliders; }
    const Vector<MyriasColliderEntry> &get_dynamic_colliders() const { return m_dynamic_colliders; }
    const Vector<MyriasSDFCell> &get_sdf_cells() const { return m_sdf_cells; }

    void update_debug_draw(RID p_parent_canvas, bool p_draw_sdf, bool p_draw_dynamic);
    void cleanup_render_resources();
};
