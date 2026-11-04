#include "physics/myrias_physics_space.h"

#include <godot_cpp/classes/area2d.hpp>
#include <godot_cpp/classes/circle_shape2d.hpp>
#include <godot_cpp/classes/collision_shape2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/rectangle_shape2d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/classes/static_body2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include "profiling/myrias_profiling.h"

MyriasPhysicsSpace::MyriasPhysicsSpace() {
    configure_sdf(Rect2(0.0f, 0.0f, 2048.0f, 1152.0f), 8.0f);
}

MyriasPhysicsSpace::~MyriasPhysicsSpace() {
    cleanup_render_resources();
}

void MyriasPhysicsSpace::configure_sdf(const Rect2 &p_bounds, float p_cell_size) {
    m_cell_size = MAX(p_cell_size, 1.0f);
    m_inv_cell_size = 1.0f / m_cell_size;
    m_sdf_bounds = p_bounds;
    m_cols = MAX((int)Math::ceil(p_bounds.size.x * m_inv_cell_size), 1);
    m_rows = MAX((int)Math::ceil(p_bounds.size.y * m_inv_cell_size), 1);

    const int num_cells = m_cols * m_rows;
    m_sdf_cells.resize(num_cells);
    m_sdf_cells.fill(MyriasSDFCell());
    m_sdf_dirty = true;
    m_sdf_texture_dirty = true;
}

bool MyriasPhysicsSpace::resolve_is_static(CollisionObject2D *p_node, MyriasColliderMode p_mode) {
    if (p_mode == COLLIDER_MODE_STATIC) {
        return true;
    }
    if (p_mode == COLLIDER_MODE_DYNAMIC) {
        return false;
    }
    return p_node != nullptr && Object::cast_to<StaticBody2D>(p_node) != nullptr;
}

static void append_shape_from_resource(const Ref<Shape2D> &p_shape, const Transform2D &p_local_xform, Vector<MyriasColliderShape> &r_shapes) {
    if (p_shape.is_null()) {
        return;
    }
    const CircleShape2D *circle = Object::cast_to<CircleShape2D>(p_shape.ptr());
    if (circle != nullptr) {
        const float r = circle->get_radius();
        if (r > 0.0f) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_CIRCLE;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_extents = Vector2(r, r);
            r_shapes.push_back(s);
        }
        return;
    }
    const RectangleShape2D *rect = Object::cast_to<RectangleShape2D>(p_shape.ptr());
    if (rect != nullptr) {
        const Vector2 half_ext = rect->get_size() * 0.5f;
        if (half_ext.x > 0.0f && half_ext.y > 0.0f) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_RECTANGLE;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_extents = half_ext;
            r_shapes.push_back(s);
        }
        return;
    }
}

void MyriasPhysicsSpace::extract_shapes_from_node(CollisionObject2D *p_node, MyriasColliderEntry &r_entry) {
    r_entry.shapes.clear();
    if (p_node == nullptr) {
        return;
    }

    r_entry.rid = p_node->get_rid();
    r_entry.is_area = Object::cast_to<Area2D>(p_node) != nullptr;

    PhysicsServer2D *ps = PhysicsServer2D::get_singleton();
    if (ps != nullptr && r_entry.rid.is_valid()) {
        const int shape_count = r_entry.is_area
                ? ps->area_get_shape_count(r_entry.rid)
                : ps->body_get_shape_count(r_entry.rid);

        for (int i = 0; i < shape_count; i++) {
            const uint32_t owner_id = p_node->shape_find_owner(i);
            if (p_node->is_shape_owner_disabled(owner_id)) {
                continue;
            }

            const RID shape_rid = r_entry.is_area
                    ? ps->area_get_shape(r_entry.rid, i)
                    : ps->body_get_shape(r_entry.rid, i);
            if (!shape_rid.is_valid()) {
                continue;
            }

            const Transform2D local_xform = r_entry.is_area
                    ? ps->area_get_shape_transform(r_entry.rid, i)
                    : ps->body_get_shape_transform(r_entry.rid, i);

            const PhysicsServer2D::ShapeType st = ps->shape_get_type(shape_rid);
            if (st == PhysicsServer2D::SHAPE_CIRCLE) {
                const float r = (float)ps->shape_get_data(shape_rid);
                if (r <= 0.0f) {
                    continue;
                }
                MyriasColliderShape s;
                s.type = MYRIAS_SHAPE_CIRCLE;
                s.shape_rid = shape_rid;
                s.local_transform = local_xform;
                s.local_extents = Vector2(r, r);
                r_entry.shapes.push_back(s);
            } else if (st == PhysicsServer2D::SHAPE_RECTANGLE) {
                const Vector2 half_extents = (Vector2)ps->shape_get_data(shape_rid);
                if (half_extents.x <= 0.0f || half_extents.y <= 0.0f) {
                    continue;
                }
                MyriasColliderShape s;
                s.type = MYRIAS_SHAPE_RECTANGLE;
                s.shape_rid = shape_rid;
                s.local_transform = local_xform;
                s.local_extents = half_extents;
                r_entry.shapes.push_back(s);
            }
        }
    }

    // Fallback 1: Inspect shape owners on CollisionObject2D if PhysicsServer2D returned 0 shapes
    if (r_entry.shapes.is_empty()) {
        const PackedInt32Array owners = p_node->get_shape_owners();
        for (int i = 0; i < owners.size(); i++) {
            const uint32_t owner_id = (uint32_t)owners[i];
            if (p_node->is_shape_owner_disabled(owner_id)) {
                continue;
            }
            const Transform2D owner_xform = p_node->shape_owner_get_transform(owner_id);
            const int sc = p_node->shape_owner_get_shape_count(owner_id);
            for (int j = 0; j < sc; j++) {
                const Ref<Shape2D> shape_ref = p_node->shape_owner_get_shape(owner_id, j);
                append_shape_from_resource(shape_ref, owner_xform, r_entry.shapes);
            }
        }
    }

    // Fallback 2: Inspect direct CollisionShape2D children if shape owners are not populated yet
    if (r_entry.shapes.is_empty()) {
        const int child_count = p_node->get_child_count();
        for (int i = 0; i < child_count; i++) {
            CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(p_node->get_child(i));
            if (cs != nullptr && !cs->is_disabled()) {
                append_shape_from_resource(cs->get_shape(), cs->get_transform(), r_entry.shapes);
            }
        }
    }
}

void MyriasPhysicsSpace::update_entry_transform_and_aabb(CollisionObject2D *p_node, MyriasColliderEntry &r_entry) {
    if (p_node == nullptr) {
        return;
    }
    r_entry.world_transform = p_node->get_global_transform();
    r_entry.collision_layer = (uint16_t)(p_node->get_collision_layer() & 0xFFFFu);
    r_entry.collision_mask = (uint16_t)(p_node->get_collision_mask() & 0xFFFFu);

    if (r_entry.shapes.is_empty()) {
        r_entry.combined_aabb = Rect2(r_entry.world_transform.get_origin(), Vector2());
        return;
    }

    MyriasColliderShape *shapes_ptr = r_entry.shapes.ptrw();
    const int count = r_entry.shapes.size();
    for (int i = 0; i < count; i++) {
        shapes_ptr[i].update_world_cache(r_entry.world_transform);
        if (i == 0) {
            r_entry.combined_aabb = shapes_ptr[i].world_aabb;
        } else {
            r_entry.combined_aabb = r_entry.combined_aabb.merge(shapes_ptr[i].world_aabb);
        }
    }
}

uint16_t MyriasPhysicsSpace::allocate_collider_id() {
    uint16_t id = m_next_collider_id++;
    if (m_next_collider_id == 0 || m_next_collider_id == 0xFFFF) {
        m_next_collider_id = 1;
    }
    return id;
}

MyriasColliderEntry *MyriasPhysicsSpace::find_collider_mut(int p_collider_id, bool *r_is_in_static, int *r_index) {
    if (p_collider_id <= 0 || p_collider_id >= 0xFFFF) {
        return nullptr;
    }
    const uint16_t cid = (uint16_t)p_collider_id;
    for (int i = 0; i < m_static_colliders.size(); i++) {
        if (m_static_colliders[i].collider_id == cid) {
            if (r_is_in_static != nullptr) {
                *r_is_in_static = true;
            }
            if (r_index != nullptr) {
                *r_index = i;
            }
            return &m_static_colliders.ptrw()[i];
        }
    }
    for (int i = 0; i < m_dynamic_colliders.size(); i++) {
        if (m_dynamic_colliders[i].collider_id == cid) {
            if (r_is_in_static != nullptr) {
                *r_is_in_static = false;
            }
            if (r_index != nullptr) {
                *r_index = i;
            }
            return &m_dynamic_colliders.ptrw()[i];
        }
    }
    return nullptr;
}

const MyriasColliderEntry *MyriasPhysicsSpace::find_collider(int p_collider_id) const {
    return const_cast<MyriasPhysicsSpace *>(this)->find_collider_mut(p_collider_id);
}

int MyriasPhysicsSpace::register_collision_object(CollisionObject2D *p_node, MyriasColliderMode p_mode) {
    if (p_node == nullptr) {
        return -1;
    }

    const ObjectID nid(p_node->get_instance_id());
    for (int i = 0; i < m_static_colliders.size(); i++) {
        if (m_static_colliders[i].node_id == nid) {
            const int cid = m_static_colliders[i].collider_id;
            set_collider_mode(cid, p_mode);
            rebuild_collider_shapes(cid);
            return cid;
        }
    }
    for (int i = 0; i < m_dynamic_colliders.size(); i++) {
        if (m_dynamic_colliders[i].node_id == nid) {
            const int cid = m_dynamic_colliders[i].collider_id;
            set_collider_mode(cid, p_mode);
            rebuild_collider_shapes(cid);
            return cid;
        }
    }

    MyriasColliderEntry entry;
    entry.collider_id = allocate_collider_id();
    entry.node_id = nid;
    entry.mode = p_mode;
    entry.resolved_static = resolve_is_static(p_node, p_mode);

    extract_shapes_from_node(p_node, entry);
    update_entry_transform_and_aabb(p_node, entry);

    if (entry.resolved_static) {
        m_static_colliders.push_back(entry);
        m_sdf_dirty = true;
    } else {
        m_dynamic_colliders.push_back(entry);
    }

    return (int)entry.collider_id;
}

void MyriasPhysicsSpace::unregister_collider(int p_collider_id) {
    bool is_static = false;
    int idx = -1;
    MyriasColliderEntry *entry = find_collider_mut(p_collider_id, &is_static, &idx);
    if (entry == nullptr) {
        return;
    }

    if (is_static) {
        m_static_colliders.remove_at(idx);
        m_sdf_dirty = true;
    } else {
        m_dynamic_colliders.remove_at(idx);
    }
}

bool MyriasPhysicsSpace::set_collider_mode(int p_collider_id, MyriasColliderMode p_mode) {
    bool is_in_static = false;
    int idx = -1;
    MyriasColliderEntry *entry = find_collider_mut(p_collider_id, &is_in_static, &idx);
    if (entry == nullptr) {
        return false;
    }

    CollisionObject2D *node = Object::cast_to<CollisionObject2D>(ObjectDB::get_instance(entry->node_id));
    const bool new_static = resolve_is_static(node, p_mode);
    entry->mode = p_mode;
    entry->resolved_static = new_static;

    if (node != nullptr) {
        if (entry->shapes.is_empty()) {
            extract_shapes_from_node(node, *entry);
        }
        update_entry_transform_and_aabb(node, *entry);
    }

    if (new_static != is_in_static) {
        MyriasColliderEntry moved = *entry;
        if (is_in_static) {
            m_static_colliders.remove_at(idx);
            m_dynamic_colliders.push_back(moved);
        } else {
            m_dynamic_colliders.remove_at(idx);
            m_static_colliders.push_back(moved);
        }
        m_sdf_dirty = true;
    } else if (new_static) {
        m_sdf_dirty = true;
    }

    return true;
}

int MyriasPhysicsSpace::get_collider_mode(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    if (entry == nullptr) {
        return (int)COLLIDER_MODE_AUTO;
    }
    return (int)entry->mode;
}

bool MyriasPhysicsSpace::rebuild_collider_shapes(int p_collider_id) {
    bool is_in_static = false;
    MyriasColliderEntry *entry = find_collider_mut(p_collider_id, &is_in_static);
    if (entry == nullptr) {
        return false;
    }

    CollisionObject2D *node = Object::cast_to<CollisionObject2D>(ObjectDB::get_instance(entry->node_id));
    if (node == nullptr) {
        return false;
    }

    extract_shapes_from_node(node, *entry);
    update_entry_transform_and_aabb(node, *entry);
    if (is_in_static) {
        m_sdf_dirty = true;
    }
    return true;
}

void MyriasPhysicsSpace::rebuild_static_sdf() {
    MYRIAS_PROFILE_ZONE("PhysicsSpace::rebuild_static_sdf");

    // 1. Validate & refresh static colliders
    for (int i = m_static_colliders.size() - 1; i >= 0; i--) {
        MyriasColliderEntry &entry = m_static_colliders.ptrw()[i];
        CollisionObject2D *node = Object::cast_to<CollisionObject2D>(ObjectDB::get_instance(entry.node_id));
        if (node == nullptr) {
            m_static_colliders.remove_at(i);
            continue;
        }
        if (entry.shapes.is_empty()) {
            extract_shapes_from_node(node, entry);
        }
        update_entry_transform_and_aabb(node, entry);
    }

    // 2. Reset SDF grid cells
    const int num_cells = m_cols * m_rows;
    if (m_sdf_cells.size() != num_cells) {
        m_sdf_cells.resize(num_cells);
    }
    MyriasSDFCell *cells = m_sdf_cells.ptrw();
    for (int i = 0; i < num_cells; i++) {
        cells[i].distance = 1e6f;
        cells[i].collision_layer = 0;
        cells[i].collider_id = 0xFFFF;
    }

    // 3. Evaluate exact analytical SDF for all static shapes
    const float ox = m_sdf_bounds.position.x;
    const float oy = m_sdf_bounds.position.y;
    const float cs = m_cell_size;

    for (int c_idx = 0; c_idx < m_static_colliders.size(); c_idx++) {
        const MyriasColliderEntry &entry = m_static_colliders[c_idx];
        const int shape_count = entry.shapes.size();
        if (shape_count == 0) {
            continue;
        }
        const MyriasColliderShape *shapes = entry.shapes.ptr();

        for (int s_idx = 0; s_idx < shape_count; s_idx++) {
            const MyriasColliderShape &shape = shapes[s_idx];

            for (int cy = 0; cy < m_rows; cy++) {
                const float wy = oy + ((float)cy + 0.5f) * cs;
                const int row_offset = cy * m_cols;

                for (int cx = 0; cx < m_cols; cx++) {
                    const float wx = ox + ((float)cx + 0.5f) * cs;
                    const float d = shape.eval_sdf(Vector2(wx, wy));

                    MyriasSDFCell &cell = cells[row_offset + cx];
                    const bool both_inside = (cell.distance <= 0.0f && d <= 0.0f);
                    const bool nearly_equal = (Math::abs(d - cell.distance) <= 1e-3f);

                    if (d < cell.distance) {
                        const uint16_t merged_layer = (both_inside || nearly_equal)
                                ? (uint16_t)(cell.collision_layer | entry.collision_layer)
                                : entry.collision_layer;
                        cell.distance = d;
                        cell.collision_layer = merged_layer;
                        cell.collider_id = entry.collider_id;
                    } else if (both_inside || nearly_equal) {
                        cell.collision_layer |= entry.collision_layer;
                    }
                }
            }
        }
    }

    m_sdf_dirty = false;
    m_sdf_texture_dirty = true;
}

void MyriasPhysicsSpace::update() {
    MYRIAS_PROFILE_ZONE("PhysicsSpace::update");

    // Check static colliders for freed nodes or lazily added shapes
    for (int i = m_static_colliders.size() - 1; i >= 0; i--) {
        MyriasColliderEntry &entry = m_static_colliders.ptrw()[i];
        CollisionObject2D *node = Object::cast_to<CollisionObject2D>(ObjectDB::get_instance(entry.node_id));
        if (node == nullptr) {
            m_static_colliders.remove_at(i);
            m_sdf_dirty = true;
            continue;
        }
        if (entry.shapes.is_empty()) {
            extract_shapes_from_node(node, entry);
            if (!entry.shapes.is_empty()) {
                m_sdf_dirty = true;
            }
        }
    }

    // Synchronize active dynamic colliders in a tight contiguous loop
    for (int i = m_dynamic_colliders.size() - 1; i >= 0; i--) {
        MyriasColliderEntry &entry = m_dynamic_colliders.ptrw()[i];
        CollisionObject2D *node = Object::cast_to<CollisionObject2D>(ObjectDB::get_instance(entry.node_id));
        if (node == nullptr) {
            m_dynamic_colliders.remove_at(i);
            continue;
        }
        if (entry.shapes.is_empty()) {
            extract_shapes_from_node(node, entry);
        }
        update_entry_transform_and_aabb(node, entry);
    }

    if (m_sdf_dirty) {
        rebuild_static_sdf();
    }
}

float MyriasPhysicsSpace::sample_sdf_distance(Vector2 p_pos, uint16_t p_mask) const {
    if (m_sdf_dirty) {
        const_cast<MyriasPhysicsSpace *>(this)->rebuild_static_sdf();
    }
    if (m_sdf_cells.is_empty() || m_cols <= 0 || m_rows <= 0) {
        return 1e6f;
    }

    const int ncx = CLAMP((int)((p_pos.x - m_sdf_bounds.position.x) * m_inv_cell_size), 0, m_cols - 1);
    const int ncy = CLAMP((int)((p_pos.y - m_sdf_bounds.position.y) * m_inv_cell_size), 0, m_rows - 1);
    const MyriasSDFCell *cells = m_sdf_cells.ptr();
    const MyriasSDFCell &nearest = cells[ncy * m_cols + ncx];

    if (nearest.collider_id == 0xFFFF || (nearest.collision_layer & p_mask) == 0) {
        return 1e6f;
    }

    const float gx = (p_pos.x - m_sdf_bounds.position.x) * m_inv_cell_size - 0.5f;
    const float gy = (p_pos.y - m_sdf_bounds.position.y) * m_inv_cell_size - 0.5f;
    const float fl_x = Math::floor(gx);
    const float fl_y = Math::floor(gy);

    const int x0 = CLAMP((int)fl_x, 0, m_cols - 1);
    const int y0 = CLAMP((int)fl_y, 0, m_rows - 1);
    const int x1 = CLAMP((int)fl_x + 1, 0, m_cols - 1);
    const int y1 = CLAMP((int)fl_y + 1, 0, m_rows - 1);

    const float tx = CLAMP(gx - fl_x, 0.0f, 1.0f);
    const float ty = CLAMP(gy - fl_y, 0.0f, 1.0f);

    const float d00 = cells[y0 * m_cols + x0].distance;
    const float d10 = cells[y0 * m_cols + x1].distance;
    const float d01 = cells[y1 * m_cols + x0].distance;
    const float d11 = cells[y1 * m_cols + x1].distance;

    const float d0 = Math::lerp(d00, d10, tx);
    const float d1 = Math::lerp(d01, d11, tx);
    return Math::lerp(d0, d1, ty);
}

Vector2 MyriasPhysicsSpace::sample_sdf_normal(Vector2 p_pos) const {
    if (m_sdf_dirty) {
        const_cast<MyriasPhysicsSpace *>(this)->rebuild_static_sdf();
    }
    const float step = m_cell_size;
    const float dx = sample_sdf_distance(p_pos + Vector2(step, 0.0f)) - sample_sdf_distance(p_pos - Vector2(step, 0.0f));
    const float dy = sample_sdf_distance(p_pos + Vector2(0.0f, step)) - sample_sdf_distance(p_pos - Vector2(0.0f, step));
    const Vector2 grad(dx, dy);
    const float len_sq = grad.length_squared();
    return len_sq > 1e-12f ? (grad / Math::sqrt(len_sq)) : Vector2(0.0f, -1.0f);
}

uint16_t MyriasPhysicsSpace::sample_sdf_layer(Vector2 p_pos) const {
    if (m_sdf_dirty) {
        const_cast<MyriasPhysicsSpace *>(this)->rebuild_static_sdf();
    }
    if (m_sdf_cells.is_empty() || m_cols <= 0 || m_rows <= 0) {
        return 0;
    }
    const int cx = CLAMP((int)((p_pos.x - m_sdf_bounds.position.x) * m_inv_cell_size), 0, m_cols - 1);
    const int cy = CLAMP((int)((p_pos.y - m_sdf_bounds.position.y) * m_inv_cell_size), 0, m_rows - 1);
    return m_sdf_cells[cy * m_cols + cx].collision_layer;
}

bool MyriasPhysicsSpace::is_collider_static(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->resolved_static : false;
}

Rect2 MyriasPhysicsSpace::get_collider_aabb(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->combined_aabb : Rect2();
}

void MyriasPhysicsSpace::update_debug_draw(RID p_parent_canvas, bool p_draw_sdf, bool p_draw_dynamic) {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs == nullptr) {
        return;
    }

    if (!p_draw_sdf && !p_draw_dynamic) {
        if (m_debug_was_drawn && m_debug_ci.is_valid()) {
            rs->canvas_item_clear(m_debug_ci);
            m_debug_was_drawn = false;
        }
        return;
    }

    if (!m_debug_ci.is_valid()) {
        m_debug_ci = rs->canvas_item_create();
        rs->canvas_item_set_z_index(m_debug_ci, 4096);
    }
    if (p_parent_canvas.is_valid() && m_debug_parent_canvas != p_parent_canvas) {
        rs->canvas_item_set_parent(m_debug_ci, p_parent_canvas);
        m_debug_parent_canvas = p_parent_canvas;
    }

    rs->canvas_item_clear(m_debug_ci);
    rs->canvas_item_set_default_texture_filter(m_debug_ci, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
    m_debug_was_drawn = true;

    // 1. Static SDF visualization
    if (p_draw_sdf && m_cols > 0 && m_rows > 0 && !m_sdf_cells.is_empty()) {
        if (m_sdf_dirty) {
            rebuild_static_sdf();
        }
        if (m_sdf_texture_dirty || m_debug_sdf_tex.is_null()) {
            PackedByteArray pixels;
            const int num_cells = m_cols * m_rows;
            pixels.resize(num_cells * 4);
            uint8_t *dst = pixels.ptrw();
            const MyriasSDFCell *cells = m_sdf_cells.ptr();
            const float boundary_band = m_cell_size * 0.75f;

            for (int i = 0; i < num_cells; i++) {
                const float d = cells[i].distance;
                uint8_t r = 0, g = 0, b = 0, a = 0;

                if (cells[i].collider_id != 0xFFFF && d < 1e5f) {
                    if (Math::abs(d) < boundary_band) {
                        // Zero-crossing boundary in bright yellow/white
                        r = 255;
                        g = 240;
                        b = 80;
                        a = 210;
                    } else if (d <= 0.0f) {
                        // Interior obstacle in warm red/orange with contour bands
                        const float band = 0.5f + 0.5f * Math::sin(d * 0.25f);
                        r = 235;
                        g = (uint8_t)CLAMP((int)(60.0f + band * 60.0f), 0, 255);
                        b = 40;
                        a = 165;
                    } else {
                        // Exterior distance field in translucent blue/teal with isolines
                        const float falloff = CLAMP(1.0f - (d / 512.0f), 0.15f, 1.0f);
                        const float iso = Math::fmod(d, 32.0f) < m_cell_size ? 1.35f : 0.85f;
                        r = (uint8_t)CLAMP((int)(20.0f * iso), 0, 255);
                        g = (uint8_t)CLAMP((int)(140.0f * falloff * iso), 0, 255);
                        b = (uint8_t)CLAMP((int)(220.0f * falloff * iso), 0, 255);
                        a = (uint8_t)CLAMP((int)(95.0f * falloff), 20, 140);
                    }
                }

                dst[i * 4 + 0] = r;
                dst[i * 4 + 1] = g;
                dst[i * 4 + 2] = b;
                dst[i * 4 + 3] = a;
            }

            Ref<Image> img = Image::create_from_data(m_cols, m_rows, false, Image::FORMAT_RGBA8, pixels);
            if (img.is_valid()) {
                if (m_debug_sdf_tex.is_null()) {
                    m_debug_sdf_tex = ImageTexture::create_from_image(img);
                } else {
                    m_debug_sdf_tex->set_image(img);
                }
            }
            m_sdf_texture_dirty = false;
        }

        if (m_debug_sdf_tex.is_valid()) {
            rs->canvas_item_add_texture_rect(m_debug_ci, m_sdf_bounds, m_debug_sdf_tex->get_rid());
        }
    }

    // 2. Dynamic Colliders visualization
    if (p_draw_dynamic) {
        const Color fill_color(0.0f, 0.9f, 1.0f, 0.28f);
        const Color outline_color(0.0f, 1.0f, 1.0f, 0.95f);
        const Color aabb_color(1.0f, 0.92f, 0.15f, 0.8f);

        for (int i = 0; i < m_dynamic_colliders.size(); i++) {
            const MyriasColliderEntry &entry = m_dynamic_colliders[i];
            for (int s = 0; s < entry.shapes.size(); s++) {
                const MyriasColliderShape &shape = entry.shapes[s];

                if (shape.type == MYRIAS_SHAPE_CIRCLE) {
                    rs->canvas_item_add_circle(m_debug_ci, shape.world_pos, shape.world_extents.x, fill_color);
                    PackedVector2Array ring;
                    constexpr int SEGMENTS = 24;
                    constexpr float TWO_PI = 6.283185307179586f;
                    ring.resize(SEGMENTS + 1);
                    for (int k = 0; k <= SEGMENTS; k++) {
                        const float ang = (float)k * (TWO_PI / (float)SEGMENTS);
                        ring.set(k, shape.world_pos + Vector2(Math::cos(ang), Math::sin(ang)) * shape.world_extents.x);
                    }
                    PackedColorArray ring_colors;
                    ring_colors.push_back(outline_color);
                    rs->canvas_item_add_polyline(m_debug_ci, ring, ring_colors, 1.5f);
                } else {
                    const Vector2 ex = shape.world_axis_x * shape.world_extents.x;
                    const Vector2 ey = shape.world_axis_y * shape.world_extents.y;
                    PackedVector2Array corners;
                    corners.push_back(shape.world_pos - ex - ey);
                    corners.push_back(shape.world_pos + ex - ey);
                    corners.push_back(shape.world_pos + ex + ey);
                    corners.push_back(shape.world_pos - ex + ey);

                    PackedColorArray poly_colors;
                    poly_colors.push_back(fill_color);
                    rs->canvas_item_add_polygon(m_debug_ci, corners, poly_colors);

                    corners.push_back(corners[0]);
                    PackedColorArray line_colors;
                    line_colors.push_back(outline_color);
                    rs->canvas_item_add_polyline(m_debug_ci, corners, line_colors, 1.5f);
                }

                // Draw world_aabb wireframe in yellow
                const Rect2 &box = shape.world_aabb;
                PackedVector2Array aabb_pts;
                aabb_pts.push_back(box.position);
                aabb_pts.push_back(box.position + Vector2(box.size.x, 0.0f));
                aabb_pts.push_back(box.position + box.size);
                aabb_pts.push_back(box.position + Vector2(0.0f, box.size.y));
                aabb_pts.push_back(box.position);
                PackedColorArray aabb_colors;
                aabb_colors.push_back(aabb_color);
                rs->canvas_item_add_polyline(m_debug_ci, aabb_pts, aabb_colors, 1.0f);
            }
        }
    }
}

void MyriasPhysicsSpace::cleanup_render_resources() {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs != nullptr && m_debug_ci.is_valid()) {
        rs->free_rid(m_debug_ci);
    }
    m_debug_ci = RID();
    m_debug_parent_canvas = RID();
    m_debug_sdf_tex.unref();
    m_debug_was_drawn = false;
}
