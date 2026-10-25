#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "godot_cpp/core/class_db.hpp"
#include <godot_cpp/classes/engine.hpp>

#include "nodes/myrias_group.h"
#include "resources/myrias_entity_model.h"
#include "server/myrias_server.h"

using namespace godot;

static MyriasServer *_server_instance = nullptr;

void initialize_example_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(MyriasEntityModel);
	GDREGISTER_CLASS(MyriasGroup);
	GDREGISTER_CLASS(MyriasServer);

	_server_instance = memnew(MyriasServer);
	Engine::get_singleton()->register_singleton("MyriasServer", _server_instance);

	//_time_manager_instance = memnew(TimeManager);
	//Engine::get_singleton()->register_singleton("TimeManager", TimeManager::get_singleton());

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
	GDExtensionBool GDE_EXPORT myrias(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_example_module);
		init_obj.register_terminator(uninitialize_example_module);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
