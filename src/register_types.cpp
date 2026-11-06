#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "godot_cpp/core/class_db.hpp"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "nodes/myrias_collider.h"
#include "nodes/myrias_collider_container.h"
#include "nodes/myrias_group.h"
#include "resources/myrias_entity_model.h"
#include "resources/myrias_trait.h"
#include "server/myrias_server.h"

using namespace godot;

static MyriasServer *_server_instance = nullptr;

static void register_myrias_project_setting(const String &p_name, const Variant &p_default, Variant::Type p_type) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return;
	}
	if (!ps->has_setting(p_name)) {
		ps->set_setting(p_name, p_default);
	}
	ps->set_initial_value(p_name, p_default);
	Dictionary prop;
	prop["name"] = p_name;
	prop["type"] = p_type;
	prop["hint"] = PROPERTY_HINT_NONE;
	prop["hint_string"] = "";
	ps->add_property_info(prop);
}

void initialize_example_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(MyriasEntityModel);
	GDREGISTER_CLASS(MyriasGroup);
	GDREGISTER_CLASS(MyriasCollider);
	GDREGISTER_CLASS(MyriasColliderContainer);
	GDREGISTER_VIRTUAL_CLASS(MyriasTrait);
	GDREGISTER_CLASS(MyriasTraitFollowSteering);
	GDREGISTER_CLASS(MyriasTraitFollowAngular);
	GDREGISTER_CLASS(MyriasTraitSineWave);
	GDREGISTER_CLASS(MyriasTraitCurveModifier);
	GDREGISTER_CLASS(MyriasTraitFaceVelocity);
	GDREGISTER_CLASS(MyriasTraitFaceNode);
	GDREGISTER_CLASS(MyriasTraitPhysics);
	GDREGISTER_CLASS(MyriasServer);

	register_myrias_project_setting("myrias/debug/draw_static_sdf", false, Variant::BOOL);
	register_myrias_project_setting("myrias/debug/draw_dynamic_colliders", false, Variant::BOOL);

	_server_instance = memnew(MyriasServer);
	Engine::get_singleton()->register_singleton("MyriasServer", _server_instance);
}

void uninitialize_example_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	Engine::get_singleton()->unregister_singleton("MyriasServer");
	memdelete(_server_instance);
	_server_instance = nullptr;
}

extern "C" {
	// Initialization.
	GDExtensionBool GDE_EXPORT myrias_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_example_module);
		init_obj.register_terminator(uninitialize_example_module);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
