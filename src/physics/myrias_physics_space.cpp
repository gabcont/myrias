#include "physics/myrias_physics_space.h"

#include <godot_cpp/classes/animatable_body2d.hpp>
#include <godot_cpp/classes/area2d.hpp>
#include <godot_cpp/classes/capsule_shape2d.hpp>
#include <godot_cpp/classes/character_body2d.hpp>
#include <godot_cpp/classes/circle_shape2d.hpp>
#include <godot_cpp/classes/collision_polygon2d.hpp>
#include <godot_cpp/classes/collision_shape2d.hpp>
#include <godot_cpp/classes/concave_polygon_shape2d.hpp>
#include <godot_cpp/classes/convex_polygon_shape2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/rectangle_shape2d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rigid_body2d.hpp>
#include <godot_cpp/classes/segment_shape2d.hpp>
#include <godot_cpp/classes/separation_ray_shape2d.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/classes/static_body2d.hpp>
#include <godot_cpp/classes/world_boundary_shape2d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
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
    return p_node != nullptr && Object::cast_to<StaticBody2D>(p_node) != nullptr && Object::cast_to<AnimatableBody2D>(p_node) == nullptr;
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
    const CapsuleShape2D *capsule = Object::cast_to<CapsuleShape2D>(p_shape.ptr());
    if (capsule != nullptr) {
        const float r = capsule->get_radius();
        const float h = MAX(capsule->get_height(), r * 2.0f);
        if (r > 0.0f) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_CAPSULE;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_extents = Vector2(r, MAX((h * 0.5f) - r, 0.0f));
            r_shapes.push_back(s);
        }
        return;
    }
    const SegmentShape2D *seg = Object::cast_to<SegmentShape2D>(p_shape.ptr());
    if (seg != nullptr) {
        MyriasColliderShape s;
        s.type = MYRIAS_SHAPE_SEGMENT;
        s.shape_rid = p_shape->get_rid();
        s.local_transform = p_local_xform;
        s.local_a = seg->get_a();
        s.local_b = seg->get_b();
        r_shapes.push_back(s);
        return;
    }
    const SeparationRayShape2D *ray = Object::cast_to<SeparationRayShape2D>(p_shape.ptr());
    if (ray != nullptr) {
        const float len = ray->get_length();
        if (len > 0.0f) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_SEPARATION_RAY;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_a = Vector2(0.0f, 0.0f);
            s.local_b = Vector2(0.0f, len);
            s.slide_on_slope = ray->get_slide_on_slope();
            r_shapes.push_back(s);
        }
        return;
    }
    const WorldBoundaryShape2D *wb = Object::cast_to<WorldBoundaryShape2D>(p_shape.ptr());
    if (wb != nullptr) {
        MyriasColliderShape s;
        s.type = MYRIAS_SHAPE_WORLD_BOUNDARY;
        s.shape_rid = p_shape->get_rid();
        s.local_transform = p_local_xform;
        const Vector2 n = wb->get_normal();
        s.local_a = n.length_squared() > 1e-8f ? n.normalized() : Vector2(0.0f, -1.0f);
        s.local_extents = Vector2(wb->get_distance(), 0.0f);
        r_shapes.push_back(s);
        return;
    }
    const ConvexPolygonShape2D *convex = Object::cast_to<ConvexPolygonShape2D>(p_shape.ptr());
    if (convex != nullptr) {
        const PackedVector2Array pts = convex->get_points();
        if (pts.size() >= 3) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_CONVEX_POLYGON;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_points.resize(pts.size());
            for (int k = 0; k < pts.size(); k++) {
                s.local_points.ptrw()[k] = pts[k];
            }
            r_shapes.push_back(s);
        }
        return;
    }
    const ConcavePolygonShape2D *concave = Object::cast_to<ConcavePolygonShape2D>(p_shape.ptr());
    if (concave != nullptr) {
        const PackedVector2Array segs = concave->get_segments();
        if (segs.size() >= 2) {
            MyriasColliderShape s;
            s.type = MYRIAS_SHAPE_CONCAVE_POLYGON;
            s.shape_rid = p_shape->get_rid();
            s.local_transform = p_local_xform;
            s.local_points.resize(segs.size());
            for (int k = 0; k < segs.size(); k++) {
                s.local_points.ptrw()[k] = segs[k];
            }
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
            const Variant data = ps->shape_get_data(shape_rid);

            switch (st) {
                case PhysicsServer2D::SHAPE_CIRCLE: {
                    const float r = (float)data;
                    if (r > 0.0f) {
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_CIRCLE;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_extents = Vector2(r, r);
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_RECTANGLE: {
                    const Vector2 half_extents = (Vector2)data;
                    if (half_extents.x > 0.0f && half_extents.y > 0.0f) {
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_RECTANGLE;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_extents = half_extents;
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_CAPSULE: {
                    float r = 0.0f;
                    float h = 0.0f;
                    if (data.get_type() == Variant::ARRAY) {
                        const Array arr = data;
                        if (arr.size() >= 2) {
                            const float a = (float)arr[0];
                            const float b = (float)arr[1];
                            h = MAX(a, b);
                            r = MIN(a, b);
                        }
                    } else if (data.get_type() == Variant::VECTOR2) {
                        const Vector2 v = data;
                        h = MAX(v.x, v.y);
                        r = MIN(v.x, v.y);
                    }
                    if (r > 0.0f) {
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_CAPSULE;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_extents = Vector2(r, MAX((h * 0.5f) - r, 0.0f));
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_SEGMENT: {
                    const Rect2 seg_rect = (Rect2)data;
                    MyriasColliderShape s;
                    s.type = MYRIAS_SHAPE_SEGMENT;
                    s.shape_rid = shape_rid;
                    s.local_transform = local_xform;
                    s.local_a = seg_rect.position;
                    s.local_b = seg_rect.size;
                    r_entry.shapes.push_back(s);
                    break;
                }
                case PhysicsServer2D::SHAPE_SEPARATION_RAY: {
                    const Dictionary d = (Dictionary)data;
                    const float len = (float)d.get("length", 20.0f);
                    const bool slide = (bool)d.get("slide_on_slope", false);
                    if (len > 0.0f) {
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_SEPARATION_RAY;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_a = Vector2(0.0f, 0.0f);
                        s.local_b = Vector2(0.0f, len);
                        s.slide_on_slope = slide;
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_WORLD_BOUNDARY: {
                    const Array arr = (Array)data;
                    if (arr.size() >= 2) {
                        const Vector2 n = (Vector2)arr[0];
                        const float dist = (float)arr[1];
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_WORLD_BOUNDARY;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_a = n.length_squared() > 1e-8f ? n.normalized() : Vector2(0.0f, -1.0f);
                        s.local_extents = Vector2(dist, 0.0f);
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_CONVEX_POLYGON: {
                    MyriasColliderShape s;
                    s.type = MYRIAS_SHAPE_CONVEX_POLYGON;
                    s.shape_rid = shape_rid;
                    s.local_transform = local_xform;
                    if (data.get_type() == Variant::PACKED_VECTOR2_ARRAY) {
                        const PackedVector2Array pts = data;
                        s.local_points.resize(pts.size());
                        for (int k = 0; k < pts.size(); k++) {
                            s.local_points.ptrw()[k] = pts[k];
                        }
                    } else if (data.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
                        const PackedFloat32Array flts = data;
                        // GodotConvexPolygonShape2D::get_data() stores [x, y, nx, ny] per vertex (stride 4) or [x, y] (stride 2)
                        const int stride = (flts.size() % 4 == 0 && flts.size() >= 12) ? 4 : 2;
                        const int count = flts.size() / stride;
                        s.local_points.resize(count);
                        for (int k = 0; k < count; k++) {
                            s.local_points.ptrw()[k] = Vector2(flts[k * stride], flts[k * stride + 1]);
                        }
                    }
                    if (s.local_points.size() >= 3) {
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                case PhysicsServer2D::SHAPE_CONCAVE_POLYGON: {
                    const PackedVector2Array segs = (PackedVector2Array)data;
                    if (segs.size() >= 2) {
                        MyriasColliderShape s;
                        s.type = MYRIAS_SHAPE_CONCAVE_POLYGON;
                        s.shape_rid = shape_rid;
                        s.local_transform = local_xform;
                        s.local_points.resize(segs.size());
                        for (int k = 0; k < segs.size(); k++) {
                            s.local_points.ptrw()[k] = segs[k];
                        }
                        r_entry.shapes.push_back(s);
                    }
                    break;
                }
                default:
                    break;
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

    // Fallback 2: Inspect direct CollisionShape2D and CollisionPolygon2D children if shape owners are not populated yet
    if (r_entry.shapes.is_empty()) {
        const int child_count = p_node->get_child_count();
        for (int i = 0; i < child_count; i++) {
            Node *child = p_node->get_child(i);
            if (CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(child)) {
                if (!cs->is_disabled()) {
                    append_shape_from_resource(cs->get_shape(), cs->get_transform(), r_entry.shapes);
                }
            } else if (CollisionPolygon2D *cp = Object::cast_to<CollisionPolygon2D>(child)) {
                if (!cp->is_disabled()) {
                    const PackedVector2Array poly = cp->get_polygon();
                    if (poly.size() >= 3) {
                        MyriasColliderShape s;
                        s.local_transform = cp->get_transform();
                        if (cp->get_build_mode() == CollisionPolygon2D::BUILD_SOLIDS) {
                            s.type = MYRIAS_SHAPE_CONVEX_POLYGON;
                            s.local_points.resize(poly.size());
                            for (int k = 0; k < poly.size(); k++) {
                                s.local_points.ptrw()[k] = poly[k];
                            }
                        } else {
                            s.type = MYRIAS_SHAPE_CONCAVE_POLYGON;
                            const int n = poly.size();
                            s.local_points.resize(n * 2);
                            for (int k = 0; k < n; k++) {
                                s.local_points.ptrw()[k * 2] = poly[k];
                                s.local_points.ptrw()[k * 2 + 1] = poly[(k + 1) % n];
                            }
                        }
                        r_entry.shapes.push_back(s);
                    }
                }
            }
        }
    }
}

void MyriasPhysicsSpace::extract_physics_params_from_node(CollisionObject2D *p_node, MyriasColliderEntry &r_entry) {
    if (p_node == nullptr) {
        return;
    }

    r_entry.rid = p_node->get_rid();
    r_entry.is_area = Object::cast_to<Area2D>(p_node) != nullptr;
    PhysicsServer2D *ps = PhysicsServer2D::get_singleton();

    if (r_entry.is_area) {
        if (ps != nullptr && r_entry.rid.is_valid()) {
            r_entry.priority = (int)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_PRIORITY);
            r_entry.gravity_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)(int)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_OVERRIDE_MODE);
            r_entry.gravity = (float)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_GRAVITY);
            r_entry.gravity_vector = (Vector2)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_VECTOR);
            r_entry.gravity_is_point = (bool)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_IS_POINT);
            r_entry.gravity_point_unit_distance = (float)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE);
            r_entry.linear_damp_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)(int)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE);
            r_entry.area_linear_damp = (float)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_LINEAR_DAMP);
            r_entry.angular_damp_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)(int)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE);
            r_entry.area_angular_damp = (float)ps->area_get_param(r_entry.rid, PhysicsServer2D::AREA_PARAM_ANGULAR_DAMP);
        }

        Area2D *area = Object::cast_to<Area2D>(p_node);
        if (area != nullptr) {
            r_entry.monitoring = area->is_monitoring();
            r_entry.monitorable = area->is_monitorable();
            r_entry.priority = area->get_priority();
            r_entry.gravity_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)area->get_gravity_space_override_mode();
            r_entry.gravity = area->get_gravity();
            r_entry.gravity_is_point = area->is_gravity_a_point();
            r_entry.gravity_vector = r_entry.gravity_is_point ? area->get_gravity_point_center() : area->get_gravity_direction();
            r_entry.gravity_point_unit_distance = area->get_gravity_point_unit_distance();
            r_entry.linear_damp_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)area->get_linear_damp_space_override_mode();
            r_entry.area_linear_damp = area->get_linear_damp();
            r_entry.angular_damp_override_mode = (PhysicsServer2D::AreaSpaceOverrideMode)area->get_angular_damp_space_override_mode();
            r_entry.area_angular_damp = area->get_angular_damp();
        }
        r_entry.inv_mass = 0.0f;
        r_entry.inv_inertia = 0.0f;
        return;
    }

    // Body Physics Metadata (!is_area)
    if (ps != nullptr && r_entry.rid.is_valid()) {
        r_entry.body_mode = ps->body_get_mode(r_entry.rid);
        r_entry.bounce = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_BOUNCE);
        r_entry.friction = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_FRICTION);
        r_entry.mass = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_MASS);
        r_entry.inertia = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_INERTIA);
        r_entry.center_of_mass = (Vector2)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_CENTER_OF_MASS);
        r_entry.gravity_scale = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_GRAVITY_SCALE);
        r_entry.linear_damp_mode = (PhysicsServer2D::BodyDampMode)(int)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_LINEAR_DAMP_MODE);
        r_entry.angular_damp_mode = (PhysicsServer2D::BodyDampMode)(int)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_ANGULAR_DAMP_MODE);
        r_entry.linear_damp = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_LINEAR_DAMP);
        r_entry.angular_damp = (float)ps->body_get_param(r_entry.rid, PhysicsServer2D::BODY_PARAM_ANGULAR_DAMP);
        r_entry.linear_velocity = (Vector2)ps->body_get_state(r_entry.rid, PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY);
        r_entry.angular_velocity = (float)ps->body_get_state(r_entry.rid, PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY);
    }

    if (StaticBody2D *sb = Object::cast_to<StaticBody2D>(p_node)) {
        r_entry.body_mode = (Object::cast_to<AnimatableBody2D>(p_node) != nullptr)
                ? PhysicsServer2D::BODY_MODE_KINEMATIC
                : PhysicsServer2D::BODY_MODE_STATIC;
        r_entry.linear_velocity = sb->get_constant_linear_velocity();
        r_entry.angular_velocity = sb->get_constant_angular_velocity();
        Ref<PhysicsMaterial> mat = sb->get_physics_material_override();
        if (mat.is_valid()) {
            r_entry.bounce = mat->is_absorbent() ? -mat->get_bounce() : mat->get_bounce();
            r_entry.friction = mat->is_rough() ? -mat->get_friction() : mat->get_friction();
        }
        r_entry.inv_mass = 0.0f;
        r_entry.inv_inertia = 0.0f;
    } else if (CharacterBody2D *cb = Object::cast_to<CharacterBody2D>(p_node)) {
        r_entry.body_mode = PhysicsServer2D::BODY_MODE_KINEMATIC;
        r_entry.linear_velocity = cb->get_velocity();
        r_entry.angular_velocity = 0.0f;
        r_entry.inv_mass = 0.0f;
        r_entry.inv_inertia = 0.0f;
    } else if (RigidBody2D *rb = Object::cast_to<RigidBody2D>(p_node)) {
        r_entry.mass = MAX(rb->get_mass(), 1e-6f);
        r_entry.gravity_scale = rb->get_gravity_scale();
        r_entry.linear_damp_mode = (PhysicsServer2D::BodyDampMode)rb->get_linear_damp_mode();
        r_entry.angular_damp_mode = (PhysicsServer2D::BodyDampMode)rb->get_angular_damp_mode();
        r_entry.linear_damp = rb->get_linear_damp();
        r_entry.angular_damp = rb->get_angular_damp();
        if (rb->get_center_of_mass_mode() == RigidBody2D::CENTER_OF_MASS_MODE_CUSTOM) {
            r_entry.center_of_mass = rb->get_center_of_mass();
        }
        Ref<PhysicsMaterial> mat = rb->get_physics_material_override();
        if (mat.is_valid()) {
            r_entry.bounce = mat->is_absorbent() ? -mat->get_bounce() : mat->get_bounce();
            r_entry.friction = mat->is_rough() ? -mat->get_friction() : mat->get_friction();
        }
        if (r_entry.linear_velocity == Vector2() && rb->get_linear_velocity() != Vector2()) {
            r_entry.linear_velocity = rb->get_linear_velocity();
        }
        if (r_entry.angular_velocity == 0.0f && rb->get_angular_velocity() != 0.0f) {
            r_entry.angular_velocity = rb->get_angular_velocity();
        }

        if (rb->is_freeze_enabled()) {
            r_entry.body_mode = (rb->get_freeze_mode() == RigidBody2D::FREEZE_MODE_STATIC)
                    ? PhysicsServer2D::BODY_MODE_STATIC
                    : PhysicsServer2D::BODY_MODE_KINEMATIC;
            r_entry.inv_mass = 0.0f;
            r_entry.inv_inertia = 0.0f;
        } else {
            r_entry.body_mode = rb->is_lock_rotation_enabled()
                    ? PhysicsServer2D::BODY_MODE_RIGID_LINEAR
                    : PhysicsServer2D::BODY_MODE_RIGID;
            r_entry.inv_mass = 1.0f / r_entry.mass;

            if (rb->get_inertia() > 0.0f) {
                r_entry.inertia = rb->get_inertia();
            }
            if (r_entry.inertia <= 0.0f && !r_entry.shapes.is_empty()) {
                const float shape_mass = r_entry.mass / (float)r_entry.shapes.size();
                float computed_inertia = 0.0f;
                for (int i = 0; i < r_entry.shapes.size(); i++) {
                    const MyriasColliderShape &s = r_entry.shapes[i];
                    const float d2 = (s.local_transform.get_origin() - r_entry.center_of_mass).length_squared();
                    switch (s.type) {
                        case MYRIAS_SHAPE_CIRCLE: {
                            const float r = s.world_extents.x > 0.0f ? s.world_extents.x : s.local_extents.x;
                            computed_inertia += shape_mass * (0.5f * r * r + d2);
                            break;
                        }
                        case MYRIAS_SHAPE_RECTANGLE: {
                            const float hx = s.world_extents.x > 0.0f ? s.world_extents.x : s.local_extents.x;
                            const float hy = s.world_extents.y > 0.0f ? s.world_extents.y : s.local_extents.y;
                            computed_inertia += shape_mass * ((1.0f / 3.0f) * (hx * hx + hy * hy) + d2);
                            break;
                        }
                        case MYRIAS_SHAPE_CAPSULE: {
                            const float r = s.world_extents.x > 0.0f ? s.world_extents.x : s.local_extents.x;
                            const float hl = s.world_extents.y > 0.0f ? s.world_extents.y : s.local_extents.y;
                            const float hy = r + hl;
                            computed_inertia += shape_mass * ((1.0f / 3.0f) * (r * r + hy * hy) + d2);
                            break;
                        }
                        case MYRIAS_SHAPE_SEGMENT:
                        case MYRIAS_SHAPE_SEPARATION_RAY: {
                            const float len_sq = (s.local_b - s.local_a).length_squared();
                            computed_inertia += shape_mass * ((1.0f / 12.0f) * MAX(len_sq, 1.0f) + d2);
                            break;
                        }
                        default: {
                            const float hx = MAX(s.world_aabb.size.x * 0.5f, 1.0f);
                            const float hy = MAX(s.world_aabb.size.y * 0.5f, 1.0f);
                            computed_inertia += shape_mass * ((1.0f / 3.0f) * (hx * hx + hy * hy) + d2);
                            break;
                        }
                    }
                }
                r_entry.inertia = computed_inertia;
            }
            r_entry.inv_inertia = (r_entry.body_mode == PhysicsServer2D::BODY_MODE_RIGID && r_entry.inertia > 1e-6f)
                    ? (1.0f / r_entry.inertia)
                    : 0.0f;
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
    } else {
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

    extract_physics_params_from_node(p_node, r_entry);
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
    const bool is_static = resolve_is_static(p_node, p_mode);

    extract_shapes_from_node(p_node, entry);
    update_entry_transform_and_aabb(p_node, entry);

    if (is_static) {
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
        if (entry.is_area) {
            continue;
        }
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

uint16_t MyriasPhysicsSpace::sample_sdf_collider_id(Vector2 p_pos) const {
    if (m_sdf_dirty) {
        const_cast<MyriasPhysicsSpace *>(this)->rebuild_static_sdf();
    }
    if (m_sdf_cells.is_empty() || m_cols <= 0 || m_rows <= 0) {
        return 0xFFFF;
    }
    const int cx = CLAMP((int)((p_pos.x - m_sdf_bounds.position.x) * m_inv_cell_size), 0, m_cols - 1);
    const int cy = CLAMP((int)((p_pos.y - m_sdf_bounds.position.y) * m_inv_cell_size), 0, m_rows - 1);
    return m_sdf_cells[cy * m_cols + cx].collider_id;
}

const MyriasColliderEntry *MyriasPhysicsSpace::get_static_collider_by_id(uint16_t p_collider_id) const {
    if (p_collider_id == 0 || p_collider_id == 0xFFFF) {
        return nullptr;
    }
    for (int i = 0; i < m_static_colliders.size(); i++) {
        if (m_static_colliders[i].collider_id == p_collider_id) {
            return &m_static_colliders[i];
        }
    }
    return nullptr;
}

bool MyriasPhysicsSpace::is_collider_static(int p_collider_id) const {
    bool is_in_static = false;
    const MyriasColliderEntry *entry = const_cast<MyriasPhysicsSpace *>(this)->find_collider_mut(p_collider_id, &is_in_static);
    return entry != nullptr && is_in_static;
}

Rect2 MyriasPhysicsSpace::get_collider_aabb(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->combined_aabb : Rect2();
}

void MyriasPhysicsSpace::apply_collider_impulse(int p_collider_id, Vector2 p_impulse, Vector2 p_world_pos) {
    MyriasColliderEntry *entry = find_collider_mut(p_collider_id);
    if (entry == nullptr || entry->is_area || entry->inv_mass <= 0.0f) {
        return;
    }
    const Vector2 offset = p_world_pos - entry->world_transform.get_origin();
    PhysicsServer2D *ps = PhysicsServer2D::get_singleton();
    if (ps != nullptr && entry->rid.is_valid()) {
        ps->body_apply_impulse(entry->rid, p_impulse, offset);
    }
    const Vector2 com_offset = p_world_pos - entry->get_world_center_of_mass();
    entry->linear_velocity += p_impulse * entry->inv_mass;
    entry->angular_velocity += com_offset.cross(p_impulse) * entry->inv_inertia;
}

float MyriasPhysicsSpace::get_collider_mass(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->mass : 0.0f;
}

float MyriasPhysicsSpace::get_collider_inv_mass(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->inv_mass : 0.0f;
}

float MyriasPhysicsSpace::get_collider_inertia(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->inertia : 0.0f;
}

float MyriasPhysicsSpace::get_collider_inv_inertia(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->inv_inertia : 0.0f;
}

float MyriasPhysicsSpace::get_collider_bounce(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->bounce : 0.0f;
}

float MyriasPhysicsSpace::get_collider_friction(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->friction : 0.0f;
}

Vector2 MyriasPhysicsSpace::get_collider_linear_velocity(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->linear_velocity : Vector2();
}

float MyriasPhysicsSpace::get_collider_angular_velocity(int p_collider_id) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->angular_velocity : 0.0f;
}

Vector2 MyriasPhysicsSpace::get_collider_point_velocity(int p_collider_id, Vector2 p_world_point) const {
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    return entry != nullptr ? entry->get_velocity_at_world_point(p_world_point) : Vector2();
}

Dictionary MyriasPhysicsSpace::get_area_space_override_info(int p_collider_id) const {
    Dictionary d;
    const MyriasColliderEntry *entry = find_collider(p_collider_id);
    if (entry == nullptr || !entry->is_area) {
        return d;
    }
    d["is_area"] = true;
    d["monitoring"] = entry->monitoring;
    d["monitorable"] = entry->monitorable;
    d["priority"] = entry->priority;
    d["gravity_override_mode"] = (int)entry->gravity_override_mode;
    d["gravity"] = entry->gravity;
    d["gravity_vector"] = entry->gravity_vector;
    d["gravity_is_point"] = entry->gravity_is_point;
    d["gravity_point_unit_distance"] = entry->gravity_point_unit_distance;
    d["linear_damp_override_mode"] = (int)entry->linear_damp_override_mode;
    d["linear_damp"] = entry->area_linear_damp;
    d["angular_damp_override_mode"] = (int)entry->angular_damp_override_mode;
    d["angular_damp"] = entry->area_angular_damp;
    return d;
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

                switch (shape.type) {
                    case MYRIAS_SHAPE_CIRCLE: {
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
                        break;
                    }
                    case MYRIAS_SHAPE_RECTANGLE: {
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
                        break;
                    }
                    case MYRIAS_SHAPE_CAPSULE: {
                        const float r = shape.world_extents.x;
                        rs->canvas_item_add_circle(m_debug_ci, shape.world_a, r, fill_color);
                        rs->canvas_item_add_circle(m_debug_ci, shape.world_b, r, fill_color);
                        const Vector2 ex = shape.world_axis_x * r;
                        PackedVector2Array body_pts;
                        body_pts.push_back(shape.world_a - ex);
                        body_pts.push_back(shape.world_a + ex);
                        body_pts.push_back(shape.world_b + ex);
                        body_pts.push_back(shape.world_b - ex);
                        PackedColorArray poly_colors;
                        poly_colors.push_back(fill_color);
                        rs->canvas_item_add_polygon(m_debug_ci, body_pts, poly_colors);
                        rs->canvas_item_add_line(m_debug_ci, shape.world_a - ex, shape.world_b - ex, outline_color, 1.5f);
                        rs->canvas_item_add_line(m_debug_ci, shape.world_a + ex, shape.world_b + ex, outline_color, 1.5f);
                        break;
                    }
                    case MYRIAS_SHAPE_SEGMENT: {
                        rs->canvas_item_add_line(m_debug_ci, shape.world_a, shape.world_b, outline_color, 2.0f);
                        break;
                    }
                    case MYRIAS_SHAPE_SEPARATION_RAY: {
                        rs->canvas_item_add_line(m_debug_ci, shape.world_a, shape.world_b, outline_color, 2.0f);
                        const Vector2 tip = shape.world_b;
                        const Vector2 back = tip - shape.world_axis_y * 6.0f;
                        rs->canvas_item_add_line(m_debug_ci, tip, back + shape.world_axis_x * 4.0f, outline_color, 1.5f);
                        rs->canvas_item_add_line(m_debug_ci, tip, back - shape.world_axis_x * 4.0f, outline_color, 1.5f);
                        break;
                    }
                    case MYRIAS_SHAPE_WORLD_BOUNDARY: {
                        const Vector2 p0 = shape.world_pos - shape.world_axis_x * 2000.0f;
                        const Vector2 p1 = shape.world_pos + shape.world_axis_x * 2000.0f;
                        rs->canvas_item_add_line(m_debug_ci, p0, p1, outline_color, 2.0f);
                        rs->canvas_item_add_line(m_debug_ci, shape.world_pos, shape.world_pos + shape.world_axis_y * 24.0f, outline_color, 2.0f);
                        break;
                    }
                    case MYRIAS_SHAPE_CONVEX_POLYGON: {
                        if (shape.world_points.size() >= 3) {
                            PackedVector2Array poly;
                            poly.resize(shape.world_points.size());
                            for (int k = 0; k < shape.world_points.size(); k++) {
                                poly.set(k, shape.world_points[k]);
                            }
                            PackedColorArray poly_colors;
                            poly_colors.push_back(fill_color);
                            rs->canvas_item_add_polygon(m_debug_ci, poly, poly_colors);
                            poly.push_back(poly[0]);
                            PackedColorArray line_colors;
                            line_colors.push_back(outline_color);
                            rs->canvas_item_add_polyline(m_debug_ci, poly, line_colors, 1.5f);
                        }
                        break;
                    }
                    case MYRIAS_SHAPE_CONCAVE_POLYGON: {
                        for (int k = 0; k + 1 < shape.world_points.size(); k += 2) {
                            rs->canvas_item_add_line(m_debug_ci, shape.world_points[k], shape.world_points[k + 1], outline_color, 2.0f);
                        }
                        break;
                    }
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
