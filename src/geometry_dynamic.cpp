#include "geometry_dynamic.hpp"
#include "geometry_common.hpp"
#include "godot_cpp/classes/collision_shape3d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/mesh_instance3d.hpp"
#include "phonon.h"
#include "server.hpp"
#include "server_init.hpp"
#include "steam_audio.hpp"

SteamAudioDynamicGeometry::SteamAudioDynamicGeometry() {}

SteamAudioDynamicGeometry::~SteamAudioDynamicGeometry() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	unregister_geometry();
	destroy_geometry();
}

void SteamAudioDynamicGeometry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_material"), &SteamAudioDynamicGeometry::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "p_material"), &SteamAudioDynamicGeometry::set_material);
	ClassDB::bind_method(D_METHOD("recalculate"), &SteamAudioDynamicGeometry::recalculate);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "SteamAudioMaterial"), "set_material", "get_material");
}

void SteamAudioDynamicGeometry::recalculate() {
	unregister_geometry();
	destroy_geometry();
	create_geometry();
	register_geometry();
}

void SteamAudioDynamicGeometry::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			ready_internal();
			break;
		case NOTIFICATION_EXIT_TREE:
			if (!Engine::get_singleton()->is_editor_hint()) {
				unregister_geometry();
			}
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			process_internal(get_physics_process_delta_time());
			break;
	}
}

void SteamAudioDynamicGeometry::ready_internal() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	set_physics_process(true);

	create_geometry();
	register_geometry();
}

void SteamAudioDynamicGeometry::process_internal(double delta) {
	if (!is_init.load()) {
		create_geometry();
		register_geometry();
		if (!is_init.load()) {
			return; // SteamAudio is not properly initialized yet
		}
	}

	if (mesh != nullptr) {
		SteamAudioServer::get_singleton()->update_dynamic_mesh_transform(
				mesh, ipl_matrix_from(get_global_transform()));
	}
}

Ref<SteamAudioMaterial> SteamAudioDynamicGeometry::get_material() { return mat; }
void SteamAudioDynamicGeometry::set_material(Ref<SteamAudioMaterial> p_material) { mat = p_material; }

PackedStringArray SteamAudioDynamicGeometry::_get_configuration_warnings() const {
	PackedStringArray res;
	if (!Object::cast_to<MeshInstance3D>(get_parent()) && !Object::cast_to<CollisionShape3D>(get_parent())) {
		res.push_back("The parent of SteamAudioGeometry must be a MeshInstance3D or a CollisionShape3D.");
	}
	return res;
}

void SteamAudioDynamicGeometry::create_geometry() {
	if (sub_scene != nullptr) {
		return;
	}
	auto gs = SteamAudioServer::get_singleton()->get_global_state();
	if (gs == nullptr) {
		return; // not inititalized yet, ret
	}

	IPLSceneSettings sub_scene_cfg = create_scene_cfg(gs->ctx);
	IPLerror err = iplSceneCreate(gs->ctx, &sub_scene_cfg, &sub_scene);
	handleErr(err);

	if (Object::cast_to<MeshInstance3D>(get_parent())) {
		meshes = create_meshes_from_mesh_inst_3d(Object::cast_to<MeshInstance3D>(get_parent()), sub_scene, mat, true);
	} else if (Object::cast_to<CollisionShape3D>(get_parent())) {
		meshes = create_meshes_from_coll_inst_3d(Object::cast_to<CollisionShape3D>(get_parent()), sub_scene, mat, true);
	} else {
		SteamAudio::log(SteamAudio::log_error, "The parent of SteamAudioGeometry must be a MeshInstance3D or a CollisionShape3D.");
		return;
	}

	for (auto m : meshes) {
		iplStaticMeshAdd(m, sub_scene);
	}

	iplSceneCommit(sub_scene);
}

void SteamAudioDynamicGeometry::destroy_geometry() {
	if (mesh != nullptr) {
		iplInstancedMeshRelease(&mesh);
		mesh = nullptr;
	}
	for (auto &m : meshes) {
		iplStaticMeshRelease(&m);
	}
	meshes.clear();
	if (sub_scene != nullptr) {
		iplSceneRelease(&sub_scene);
		sub_scene = nullptr;
	}
	is_init.store(false);
}

void SteamAudioDynamicGeometry::register_geometry() {
	auto gs = SteamAudioServer::get_singleton()->get_global_state();
	if (gs == nullptr) {
		return; // not inititalized yet, ret
	}

	if (mesh == nullptr) {
		if (sub_scene == nullptr) {
			return;
		}
		IPLInstancedMeshSettings mesh_cfg{ sub_scene, ipl_matrix_from(get_global_transform()) };
		IPLerror err = iplInstancedMeshCreate(gs->scene, &mesh_cfg, &mesh);
		handleErr(err);
	}

	is_init.store(true);
	SteamAudioServer::get_singleton()->add_dynamic_mesh(mesh);
}

void SteamAudioDynamicGeometry::unregister_geometry() {
	if (mesh == nullptr) {
		return;
	}
	auto srv = SteamAudioServer::get_singleton();
	if (srv != nullptr) {
		srv->remove_dynamic_mesh(mesh);
	}
}
