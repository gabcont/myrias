#pragma once

#include <memory>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "traits/myrias_traits.h"
#include "traits/trait_follow_steering.h"
#include "traits/trait_follow_angular.h"
#include "traits/trait_sine_wave.h"
#include "traits/trait_curve_modifier.h"
#include "traits/trait_face_velocity.h"
#include "traits/trait_face_node.h"
#include "traits/trait_physics.h"

using namespace godot;

class MyriasTrait : public Resource {
    GDCLASS(MyriasTrait, Resource);

protected:
    static void _bind_methods();

public:
    virtual MyriasTraitType get_trait_type() const { return (MyriasTraitType)0; }
    virtual uint32_t get_trait_flag() const { return (uint32_t)get_trait_type(); }
    virtual std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const { return nullptr; }
    void apply_to_group(int p_group_id, Node2D *p_owner = nullptr) const;
};

class MyriasTraitFollowSteering : public MyriasTrait {
    GDCLASS(MyriasTraitFollowSteering, MyriasTrait);

private:
    NodePath m_target_node_path;
    std::shared_ptr<FollowSteeringConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitFollowSteering();

    void set_target_node_path(const NodePath &p_path);
    NodePath get_target_node_path() const;

    void set_target_position(const Vector2 &p_pos);
    Vector2 get_target_position() const;
    void set_target_pos(const Vector2 &p_pos) { set_target_position(p_pos); }
    Vector2 get_target_pos() const { return get_target_position(); }

    void set_turn_force(float p_force);
    float get_turn_force() const;

    void set_max_speed(float p_speed);
    float get_max_speed() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_FOLLOW_STEERING; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override;
};

class MyriasTraitFollowAngular : public MyriasTrait {
    GDCLASS(MyriasTraitFollowAngular, MyriasTrait);

private:
    NodePath m_target_node_path;
    std::shared_ptr<FollowAngularConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitFollowAngular();

    void set_target_node_path(const NodePath &p_path);
    NodePath get_target_node_path() const;

    void set_target_position(const Vector2 &p_pos);
    Vector2 get_target_position() const;
    void set_target_pos(const Vector2 &p_pos) { set_target_position(p_pos); }
    Vector2 get_target_pos() const { return get_target_position(); }

    void set_turn_rate(float p_rate);
    float get_turn_rate() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_FOLLOW_ANGULAR; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override;
};

class MyriasTraitSineWave : public MyriasTrait {
    GDCLASS(MyriasTraitSineWave, MyriasTrait);

private:
    std::shared_ptr<SineWaveConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitSineWave();

    void set_amplitude(float p_amplitude);
    float get_amplitude() const;

    void set_frequency(float p_frequency);
    float get_frequency() const;

    void set_phase(float p_phase);
    float get_phase() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_SINE_WAVE; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override { return m_config; }
};

class MyriasTraitCurveModifier : public MyriasTrait {
    GDCLASS(MyriasTraitCurveModifier, MyriasTrait);

private:
    Ref<Curve> m_curve;
    std::shared_ptr<CurveModifierConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitCurveModifier();

    void set_curve(const Ref<Curve> &p_curve);
    Ref<Curve> get_curve() const;

    void set_target_property(int p_property);
    int get_target_property() const;

    void set_duration(float p_duration);
    float get_duration() const;

    void set_multiplier(float p_multiplier);
    float get_multiplier() const;

    void set_loop(bool p_loop);
    bool get_loop() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_CURVE_MODIFIER; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override;
};

class MyriasTraitFaceVelocity : public MyriasTrait {
    GDCLASS(MyriasTraitFaceVelocity, MyriasTrait);

private:
    std::shared_ptr<FaceVelocityConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitFaceVelocity();

    void set_angle_offset(float p_rad);
    float get_angle_offset() const;

    void set_angle_offset_degrees(float p_degrees);
    float get_angle_offset_degrees() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_FACE_VELOCITY; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override { return m_config; }
};

class MyriasTraitFaceNode : public MyriasTrait {
    GDCLASS(MyriasTraitFaceNode, MyriasTrait);

private:
    NodePath m_target_node_path;
    std::shared_ptr<FaceNodeConfig> m_config;

protected:
    static void _bind_methods();

public:
    MyriasTraitFaceNode();

    void set_target_node_path(const NodePath &p_path);
    NodePath get_target_node_path() const;

    void set_target_position(const Vector2 &p_pos);
    Vector2 get_target_position() const;
    void set_target_pos(const Vector2 &p_pos) { set_target_position(p_pos); }
    Vector2 get_target_pos() const { return get_target_position(); }

    void set_angle_offset(float p_rad);
    float get_angle_offset() const;

    void set_angle_offset_degrees(float p_degrees);
    float get_angle_offset_degrees() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_FACE_NODE; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override;
};

class MyriasTraitPhysics : public MyriasTrait {
    GDCLASS(MyriasTraitPhysics, MyriasTrait);

private:
    float m_mass = 1.0f;
    float m_bounce = 0.5f;
    float m_friction = 0.2f;
    Ref<PhysicsMaterial> m_physics_material;
    float m_gravity_scale = 1.0f;
    float m_linear_damp = 0.0f;
    std::shared_ptr<PhysicsTraitConfig> m_config;

    void _on_material_changed();

protected:
    static void _bind_methods();

public:
    MyriasTraitPhysics();
    ~MyriasTraitPhysics();

    void set_mass(float p_mass);
    float get_mass() const;

    void set_bounce(float p_bounce);
    float get_bounce() const;

    void set_friction(float p_friction);
    float get_friction() const;

    void set_physics_material(const Ref<PhysicsMaterial> &p_material);
    Ref<PhysicsMaterial> get_physics_material() const;

    void set_gravity_scale(float p_scale);
    float get_gravity_scale() const;

    void set_linear_damp(float p_damp);
    float get_linear_damp() const;

    MyriasTraitType get_trait_type() const override { return TRAIT_PHYSICS; }
    std::shared_ptr<TraitConfig> build_config(Node2D *p_owner = nullptr) const override;
};
