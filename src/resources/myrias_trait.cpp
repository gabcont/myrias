#include "resources/myrias_trait.h"

#include "server/myrias_server.h"

void MyriasTrait::_bind_methods() {
    // Base class binding
}

void MyriasTrait::apply_to_group(int p_group_id, Node2D *p_owner) const {
    MyriasServer *server = MyriasServer::get_singleton();
    if (server != nullptr) {
        server->configure_trait(p_group_id, get_trait_type(), build_config(p_owner));
    }
}

// 1. Follow Steering
MyriasTraitFollowSteering::MyriasTraitFollowSteering() {
    m_config = std::make_shared<FollowSteeringConfig>();
}

void MyriasTraitFollowSteering::set_target_node_path(const NodePath &p_path) {
    m_target_node_path = p_path;
    emit_changed();
}
NodePath MyriasTraitFollowSteering::get_target_node_path() const {
    return m_target_node_path;
}

void MyriasTraitFollowSteering::set_target_position(const Vector2 &p_pos) {
    m_config->target_pos = p_pos;
    emit_changed();
}
Vector2 MyriasTraitFollowSteering::get_target_position() const {
    return m_config->target_pos;
}

void MyriasTraitFollowSteering::set_turn_force(float p_force) {
    m_config->turn_force = p_force;
    emit_changed();
}
float MyriasTraitFollowSteering::get_turn_force() const {
    return m_config->turn_force;
}

void MyriasTraitFollowSteering::set_max_speed(float p_speed) {
    m_config->max_speed = MAX(p_speed, 0.0f);
    emit_changed();
}
float MyriasTraitFollowSteering::get_max_speed() const {
    return m_config->max_speed;
}

std::shared_ptr<TraitConfig> MyriasTraitFollowSteering::build_config(Node2D *p_owner) const {
    auto cfg = std::make_shared<FollowSteeringConfig>(*m_config);
    if (p_owner != nullptr && !m_target_node_path.is_empty() && p_owner->is_inside_tree()) {
        Node2D *t = Object::cast_to<Node2D>(p_owner->get_node_or_null(m_target_node_path));
        if (t != nullptr) {
            cfg->target_node_id = t->get_instance_id();
            cfg->target_pos = t->get_global_position();
        }
    }
    return cfg;
}

void MyriasTraitFollowSteering::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_target_node_path", "target_node_path"), &MyriasTraitFollowSteering::set_target_node_path);
    ClassDB::bind_method(D_METHOD("get_target_node_path"), &MyriasTraitFollowSteering::get_target_node_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_node_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"),
            "set_target_node_path", "get_target_node_path");

    ClassDB::bind_method(D_METHOD("set_target_position", "target_position"), &MyriasTraitFollowSteering::set_target_position);
    ClassDB::bind_method(D_METHOD("get_target_position"), &MyriasTraitFollowSteering::get_target_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_position"), "set_target_position", "get_target_position");

    ClassDB::bind_method(D_METHOD("set_target_pos", "target_pos"), &MyriasTraitFollowSteering::set_target_pos);
    ClassDB::bind_method(D_METHOD("get_target_pos"), &MyriasTraitFollowSteering::get_target_pos);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_pos"), "set_target_pos", "get_target_pos");

    ClassDB::bind_method(D_METHOD("set_turn_force", "turn_force"), &MyriasTraitFollowSteering::set_turn_force);
    ClassDB::bind_method(D_METHOD("get_turn_force"), &MyriasTraitFollowSteering::get_turn_force);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turn_force", PROPERTY_HINT_RANGE, "0.0,10000.0,1.0"), "set_turn_force", "get_turn_force");

    ClassDB::bind_method(D_METHOD("set_max_speed", "max_speed"), &MyriasTraitFollowSteering::set_max_speed);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &MyriasTraitFollowSteering::get_max_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed", PROPERTY_HINT_RANGE, "0.0,10000.0,1.0"), "set_max_speed", "get_max_speed");
}

// 2. Follow Angular
MyriasTraitFollowAngular::MyriasTraitFollowAngular() {
    m_config = std::make_shared<FollowAngularConfig>();
}

void MyriasTraitFollowAngular::set_target_node_path(const NodePath &p_path) {
    m_target_node_path = p_path;
    emit_changed();
}
NodePath MyriasTraitFollowAngular::get_target_node_path() const {
    return m_target_node_path;
}

void MyriasTraitFollowAngular::set_target_position(const Vector2 &p_pos) {
    m_config->target_pos = p_pos;
    emit_changed();
}
Vector2 MyriasTraitFollowAngular::get_target_position() const {
    return m_config->target_pos;
}

void MyriasTraitFollowAngular::set_turn_rate(float p_rate) {
    m_config->turn_rate = p_rate;
    emit_changed();
}
float MyriasTraitFollowAngular::get_turn_rate() const {
    return m_config->turn_rate;
}

std::shared_ptr<TraitConfig> MyriasTraitFollowAngular::build_config(Node2D *p_owner) const {
    auto cfg = std::make_shared<FollowAngularConfig>(*m_config);
    if (p_owner != nullptr && !m_target_node_path.is_empty() && p_owner->is_inside_tree()) {
        Node2D *t = Object::cast_to<Node2D>(p_owner->get_node_or_null(m_target_node_path));
        if (t != nullptr) {
            cfg->target_node_id = t->get_instance_id();
            cfg->target_pos = t->get_global_position();
        }
    }
    return cfg;
}

void MyriasTraitFollowAngular::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_target_node_path", "target_node_path"), &MyriasTraitFollowAngular::set_target_node_path);
    ClassDB::bind_method(D_METHOD("get_target_node_path"), &MyriasTraitFollowAngular::get_target_node_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_node_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"),
            "set_target_node_path", "get_target_node_path");

    ClassDB::bind_method(D_METHOD("set_target_position", "target_position"), &MyriasTraitFollowAngular::set_target_position);
    ClassDB::bind_method(D_METHOD("get_target_position"), &MyriasTraitFollowAngular::get_target_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_position"), "set_target_position", "get_target_position");

    ClassDB::bind_method(D_METHOD("set_target_pos", "target_pos"), &MyriasTraitFollowAngular::set_target_pos);
    ClassDB::bind_method(D_METHOD("get_target_pos"), &MyriasTraitFollowAngular::get_target_pos);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_pos"), "set_target_pos", "get_target_pos");

    ClassDB::bind_method(D_METHOD("set_turn_rate", "turn_rate"), &MyriasTraitFollowAngular::set_turn_rate);
    ClassDB::bind_method(D_METHOD("get_turn_rate"), &MyriasTraitFollowAngular::get_turn_rate);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turn_rate", PROPERTY_HINT_RANGE, "0.0,62.83,0.01"), "set_turn_rate", "get_turn_rate");
}

// 3. Sine Wave
MyriasTraitSineWave::MyriasTraitSineWave() {
    m_config = std::make_shared<SineWaveConfig>();
}

void MyriasTraitSineWave::set_amplitude(float p_amplitude) {
    m_config->amplitude = p_amplitude;
    emit_changed();
}
float MyriasTraitSineWave::get_amplitude() const {
    return m_config->amplitude;
}

void MyriasTraitSineWave::set_frequency(float p_frequency) {
    m_config->frequency = p_frequency;
    emit_changed();
}
float MyriasTraitSineWave::get_frequency() const {
    return m_config->frequency;
}

void MyriasTraitSineWave::set_phase(float p_phase) {
    m_config->phase = p_phase;
    emit_changed();
}
float MyriasTraitSineWave::get_phase() const {
    return m_config->phase;
}

void MyriasTraitSineWave::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &MyriasTraitSineWave::set_amplitude);
    ClassDB::bind_method(D_METHOD("get_amplitude"), &MyriasTraitSineWave::get_amplitude);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude", PROPERTY_HINT_RANGE, "0.0,1000.0,0.1"), "set_amplitude", "get_amplitude");

    ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &MyriasTraitSineWave::set_frequency);
    ClassDB::bind_method(D_METHOD("get_frequency"), &MyriasTraitSineWave::get_frequency);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "0.0,50.0,0.01"), "set_frequency", "get_frequency");

    ClassDB::bind_method(D_METHOD("set_phase", "phase"), &MyriasTraitSineWave::set_phase);
    ClassDB::bind_method(D_METHOD("get_phase"), &MyriasTraitSineWave::get_phase);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase", PROPERTY_HINT_RANGE, "0.0,6.28318,0.01"), "set_phase", "get_phase");
}

// 4. Curve Modifier
MyriasTraitCurveModifier::MyriasTraitCurveModifier() {
    m_config = std::make_shared<CurveModifierConfig>();
}

void MyriasTraitCurveModifier::set_curve(const Ref<Curve> &p_curve) {
    m_curve = p_curve;
    m_config->bake(m_curve);
    emit_changed();
}
Ref<Curve> MyriasTraitCurveModifier::get_curve() const {
    return m_curve;
}

void MyriasTraitCurveModifier::set_target_property(int p_property) {
    m_config->target_property = CLAMP(p_property, 0, 2);
    emit_changed();
}
int MyriasTraitCurveModifier::get_target_property() const {
    return m_config->target_property;
}

void MyriasTraitCurveModifier::set_duration(float p_duration) {
    m_config->duration = MAX(p_duration, 0.001f);
    emit_changed();
}
float MyriasTraitCurveModifier::get_duration() const {
    return m_config->duration;
}

void MyriasTraitCurveModifier::set_multiplier(float p_multiplier) {
    m_config->multiplier = p_multiplier;
    emit_changed();
}
float MyriasTraitCurveModifier::get_multiplier() const {
    return m_config->multiplier;
}

void MyriasTraitCurveModifier::set_loop(bool p_loop) {
    m_config->loop = p_loop;
    emit_changed();
}
bool MyriasTraitCurveModifier::get_loop() const {
    return m_config->loop;
}

void MyriasTraitCurveModifier::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_curve", "curve"), &MyriasTraitCurveModifier::set_curve);
    ClassDB::bind_method(D_METHOD("get_curve"), &MyriasTraitCurveModifier::get_curve);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_curve", "get_curve");

    ClassDB::bind_method(D_METHOD("set_target_property", "target_property"), &MyriasTraitCurveModifier::set_target_property);
    ClassDB::bind_method(D_METHOD("get_target_property"), &MyriasTraitCurveModifier::get_target_property);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "target_property", PROPERTY_HINT_ENUM, "Scale,Rotation,Speed"), "set_target_property", "get_target_property");

    ClassDB::bind_method(D_METHOD("set_duration", "duration"), &MyriasTraitCurveModifier::set_duration);
    ClassDB::bind_method(D_METHOD("get_duration"), &MyriasTraitCurveModifier::get_duration);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0.001,100.0,0.01"), "set_duration", "get_duration");

    ClassDB::bind_method(D_METHOD("set_multiplier", "multiplier"), &MyriasTraitCurveModifier::set_multiplier);
    ClassDB::bind_method(D_METHOD("get_multiplier"), &MyriasTraitCurveModifier::get_multiplier);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "multiplier", PROPERTY_HINT_RANGE, "-1000.0,1000.0,0.1"), "set_multiplier", "get_multiplier");

    ClassDB::bind_method(D_METHOD("set_loop", "loop"), &MyriasTraitCurveModifier::set_loop);
    ClassDB::bind_method(D_METHOD("get_loop"), &MyriasTraitCurveModifier::get_loop);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "get_loop");
}

std::shared_ptr<TraitConfig> MyriasTraitCurveModifier::build_config(Node2D *p_owner) const {
    if (m_curve.is_valid()) {
        m_config->bake(m_curve);
    }
    return m_config;
}

// 5. Face Velocity
MyriasTraitFaceVelocity::MyriasTraitFaceVelocity() {
    m_config = std::make_shared<FaceVelocityConfig>();
}

void MyriasTraitFaceVelocity::set_angle_offset(float p_rad) {
    m_config->angle_offset = p_rad;
    emit_changed();
}
float MyriasTraitFaceVelocity::get_angle_offset() const {
    return m_config->angle_offset;
}

void MyriasTraitFaceVelocity::set_angle_offset_degrees(float p_degrees) {
    m_config->angle_offset = Math::deg_to_rad(p_degrees);
    emit_changed();
}
float MyriasTraitFaceVelocity::get_angle_offset_degrees() const {
    return Math::rad_to_deg(m_config->angle_offset);
}

void MyriasTraitFaceVelocity::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_angle_offset", "angle_offset"), &MyriasTraitFaceVelocity::set_angle_offset);
    ClassDB::bind_method(D_METHOD("get_angle_offset"), &MyriasTraitFaceVelocity::get_angle_offset);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_offset", PROPERTY_HINT_RANGE, "-6.28318,6.28318,0.01"),
            "set_angle_offset", "get_angle_offset");

    ClassDB::bind_method(D_METHOD("set_angle_offset_degrees", "angle_offset_degrees"), &MyriasTraitFaceVelocity::set_angle_offset_degrees);
    ClassDB::bind_method(D_METHOD("get_angle_offset_degrees"), &MyriasTraitFaceVelocity::get_angle_offset_degrees);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_offset_degrees", PROPERTY_HINT_RANGE, "-360.0,360.0,0.1"),
            "set_angle_offset_degrees", "get_angle_offset_degrees");
}

// 6. Face Node
MyriasTraitFaceNode::MyriasTraitFaceNode() {
    m_config = std::make_shared<FaceNodeConfig>();
}

void MyriasTraitFaceNode::set_target_node_path(const NodePath &p_path) {
    m_target_node_path = p_path;
    emit_changed();
}
NodePath MyriasTraitFaceNode::get_target_node_path() const {
    return m_target_node_path;
}

void MyriasTraitFaceNode::set_target_position(const Vector2 &p_pos) {
    m_config->target_pos = p_pos;
    emit_changed();
}
Vector2 MyriasTraitFaceNode::get_target_position() const {
    return m_config->target_pos;
}

void MyriasTraitFaceNode::set_angle_offset(float p_rad) {
    m_config->angle_offset = p_rad;
    emit_changed();
}
float MyriasTraitFaceNode::get_angle_offset() const {
    return m_config->angle_offset;
}

void MyriasTraitFaceNode::set_angle_offset_degrees(float p_degrees) {
    m_config->angle_offset = Math::deg_to_rad(p_degrees);
    emit_changed();
}
float MyriasTraitFaceNode::get_angle_offset_degrees() const {
    return Math::rad_to_deg(m_config->angle_offset);
}

std::shared_ptr<TraitConfig> MyriasTraitFaceNode::build_config(Node2D *p_owner) const {
    auto cfg = std::make_shared<FaceNodeConfig>(*m_config);
    if (p_owner != nullptr && !m_target_node_path.is_empty() && p_owner->is_inside_tree()) {
        Node2D *t = Object::cast_to<Node2D>(p_owner->get_node_or_null(m_target_node_path));
        if (t != nullptr) {
            cfg->target_node_id = t->get_instance_id();
            cfg->target_pos = t->get_global_position();
        }
    }
    return cfg;
}

void MyriasTraitFaceNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_target_node_path", "target_node_path"), &MyriasTraitFaceNode::set_target_node_path);
    ClassDB::bind_method(D_METHOD("get_target_node_path"), &MyriasTraitFaceNode::get_target_node_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_node_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"),
            "set_target_node_path", "get_target_node_path");

    ClassDB::bind_method(D_METHOD("set_target_position", "target_position"), &MyriasTraitFaceNode::set_target_position);
    ClassDB::bind_method(D_METHOD("get_target_position"), &MyriasTraitFaceNode::get_target_position);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_position"), "set_target_position", "get_target_position");

    ClassDB::bind_method(D_METHOD("set_target_pos", "target_pos"), &MyriasTraitFaceNode::set_target_pos);
    ClassDB::bind_method(D_METHOD("get_target_pos"), &MyriasTraitFaceNode::get_target_pos);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "target_pos"), "set_target_pos", "get_target_pos");

    ClassDB::bind_method(D_METHOD("set_angle_offset", "angle_offset"), &MyriasTraitFaceNode::set_angle_offset);
    ClassDB::bind_method(D_METHOD("get_angle_offset"), &MyriasTraitFaceNode::get_angle_offset);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_offset", PROPERTY_HINT_RANGE, "-6.28318,6.28318,0.01"),
            "set_angle_offset", "get_angle_offset");

    ClassDB::bind_method(D_METHOD("set_angle_offset_degrees", "angle_offset_degrees"), &MyriasTraitFaceNode::set_angle_offset_degrees);
    ClassDB::bind_method(D_METHOD("get_angle_offset_degrees"), &MyriasTraitFaceNode::get_angle_offset_degrees);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_offset_degrees", PROPERTY_HINT_RANGE, "-360.0,360.0,0.1"),
            "set_angle_offset_degrees", "get_angle_offset_degrees");
}

// 7. Physics
MyriasTraitPhysics::MyriasTraitPhysics() {
    m_config = std::make_shared<PhysicsTraitConfig>();
}

MyriasTraitPhysics::~MyriasTraitPhysics() {
    if (m_physics_material.is_valid() && m_physics_material->is_connected("changed", Callable(this, "_on_material_changed"))) {
        m_physics_material->disconnect("changed", Callable(this, "_on_material_changed"));
    }
}

void MyriasTraitPhysics::_on_material_changed() {
    emit_changed();
}

void MyriasTraitPhysics::set_mass(float p_mass) {
    m_mass = MAX(p_mass, 1e-6f);
    emit_changed();
}
float MyriasTraitPhysics::get_mass() const {
    return m_mass;
}

void MyriasTraitPhysics::set_bounce(float p_bounce) {
    m_bounce = CLAMP(p_bounce, 0.0f, 1.0f);
    emit_changed();
}
float MyriasTraitPhysics::get_bounce() const {
    return m_bounce;
}

void MyriasTraitPhysics::set_friction(float p_friction) {
    m_friction = CLAMP(p_friction, 0.0f, 1.0f);
    emit_changed();
}
float MyriasTraitPhysics::get_friction() const {
    return m_friction;
}

void MyriasTraitPhysics::set_physics_material(const Ref<PhysicsMaterial> &p_material) {
    if (m_physics_material.is_valid() && m_physics_material->is_connected("changed", Callable(this, "_on_material_changed"))) {
        m_physics_material->disconnect("changed", Callable(this, "_on_material_changed"));
    }
    m_physics_material = p_material;
    if (m_physics_material.is_valid() && !m_physics_material->is_connected("changed", Callable(this, "_on_material_changed"))) {
        m_physics_material->connect("changed", Callable(this, "_on_material_changed"));
    }
    emit_changed();
}
Ref<PhysicsMaterial> MyriasTraitPhysics::get_physics_material() const {
    return m_physics_material;
}

void MyriasTraitPhysics::set_gravity_scale(float p_scale) {
    m_gravity_scale = p_scale;
    emit_changed();
}
float MyriasTraitPhysics::get_gravity_scale() const {
    return m_gravity_scale;
}

void MyriasTraitPhysics::set_linear_damp(float p_damp) {
    m_linear_damp = MAX(p_damp, 0.0f);
    emit_changed();
}
float MyriasTraitPhysics::get_linear_damp() const {
    return m_linear_damp;
}

std::shared_ptr<TraitConfig> MyriasTraitPhysics::build_config(Node2D *p_owner) const {
    (void)p_owner;
    m_config->mass = MAX(m_mass, 1e-6f);
    m_config->inv_mass = 1.0f / m_config->mass;
    if (m_physics_material.is_valid()) {
        const float b = CLAMP(m_physics_material->get_bounce(), 0.0f, 1.0f);
        const float f = CLAMP(m_physics_material->get_friction(), 0.0f, 1.0f);
        m_config->bounce = m_physics_material->is_absorbent() ? -b : b;
        m_config->friction = m_physics_material->is_rough() ? -f : f;
    } else {
        m_config->bounce = CLAMP(m_bounce, 0.0f, 1.0f);
        m_config->friction = CLAMP(m_friction, 0.0f, 1.0f);
    }
    m_config->gravity_scale = m_gravity_scale;
    m_config->linear_damp = MAX(m_linear_damp, 0.0f);
    return m_config;
}

void MyriasTraitPhysics::_bind_methods() {
    ClassDB::bind_method(D_METHOD("_on_material_changed"), &MyriasTraitPhysics::_on_material_changed);

    ClassDB::bind_method(D_METHOD("set_mass", "mass"), &MyriasTraitPhysics::set_mass);
    ClassDB::bind_method(D_METHOD("get_mass"), &MyriasTraitPhysics::get_mass);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mass", PROPERTY_HINT_RANGE, "0.001,1000.0,0.01"), "set_mass", "get_mass");

    ClassDB::bind_method(D_METHOD("set_bounce", "bounce"), &MyriasTraitPhysics::set_bounce);
    ClassDB::bind_method(D_METHOD("get_bounce"), &MyriasTraitPhysics::get_bounce);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_bounce", "get_bounce");

    ClassDB::bind_method(D_METHOD("set_friction", "friction"), &MyriasTraitPhysics::set_friction);
    ClassDB::bind_method(D_METHOD("get_friction"), &MyriasTraitPhysics::get_friction);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_friction", "get_friction");

    ClassDB::bind_method(D_METHOD("set_physics_material", "physics_material"), &MyriasTraitPhysics::set_physics_material);
    ClassDB::bind_method(D_METHOD("get_physics_material"), &MyriasTraitPhysics::get_physics_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "physics_material", PROPERTY_HINT_RESOURCE_TYPE, "PhysicsMaterial"),
            "set_physics_material", "get_physics_material");

    ClassDB::bind_method(D_METHOD("set_gravity_scale", "gravity_scale"), &MyriasTraitPhysics::set_gravity_scale);
    ClassDB::bind_method(D_METHOD("get_gravity_scale"), &MyriasTraitPhysics::get_gravity_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "-10.0,10.0,0.05"),
            "set_gravity_scale", "get_gravity_scale");

    ClassDB::bind_method(D_METHOD("set_linear_damp", "linear_damp"), &MyriasTraitPhysics::set_linear_damp);
    ClassDB::bind_method(D_METHOD("get_linear_damp"), &MyriasTraitPhysics::get_linear_damp);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_damp", PROPERTY_HINT_RANGE, "0.0,100.0,0.05"),
            "set_linear_damp", "get_linear_damp");
}
