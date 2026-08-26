#include "probe_volume.hpp"
#include "geometry.hpp"
#include "geometry_common.hpp"
#include <cstdio>
#include <cstring>
#include "godot_cpp/classes/collision_shape3d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/mesh_instance3d.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "server.hpp"
#include "steam_audio.hpp"

void SteamAudioProbeBatchData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bytes"), &SteamAudioProbeBatchData::get_bytes);
	ClassDB::bind_method(D_METHOD("set_bytes", "p_bytes"), &SteamAudioProbeBatchData::set_bytes);
	ClassDB::bind_method(D_METHOD("get_probe_count"), &SteamAudioProbeBatchData::get_probe_count);
	ClassDB::bind_method(D_METHOD("set_probe_count", "p_count"), &SteamAudioProbeBatchData::set_probe_count);
	ClassDB::bind_method(D_METHOD("is_pathing_baked"), &SteamAudioProbeBatchData::is_pathing_baked);
	ClassDB::bind_method(D_METHOD("set_pathing_baked", "p_baked"), &SteamAudioProbeBatchData::set_pathing_baked);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"), "set_bytes", "get_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "probe_count"), "set_probe_count", "get_probe_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pathing_baked"), "set_pathing_baked", "is_pathing_baked");
}

void SteamAudioProbeVolume::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate_probes"), &SteamAudioProbeVolume::generate_probes);
	ClassDB::bind_method(D_METHOD("bake_pathing"), &SteamAudioProbeVolume::bake_pathing);
	ClassDB::bind_method(D_METHOD("get_probe_count"), &SteamAudioProbeVolume::get_probe_count);

	ClassDB::bind_method(D_METHOD("get_size"), &SteamAudioProbeVolume::get_size);
	ClassDB::bind_method(D_METHOD("set_size", "p_size"), &SteamAudioProbeVolume::set_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size"), "set_size", "get_size");

	ClassDB::bind_method(D_METHOD("get_generation"), &SteamAudioProbeVolume::get_generation);
	ClassDB::bind_method(D_METHOD("set_generation", "p_generation"), &SteamAudioProbeVolume::set_generation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "generation", PROPERTY_HINT_ENUM, "Centroid,Uniform Floor"),
			"set_generation", "get_generation");

	ClassDB::bind_method(D_METHOD("get_spacing"), &SteamAudioProbeVolume::get_spacing);
	ClassDB::bind_method(D_METHOD("set_spacing", "p_spacing"), &SteamAudioProbeVolume::set_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spacing", PROPERTY_HINT_RANGE, "0.5,20.0,0.1"), "set_spacing", "get_spacing");

	ClassDB::bind_method(D_METHOD("get_height"), &SteamAudioProbeVolume::get_height);
	ClassDB::bind_method(D_METHOD("set_height", "p_height"), &SteamAudioProbeVolume::set_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.1,5.0,0.1"), "set_height", "get_height");

	ADD_GROUP("Bake", "");
	ClassDB::bind_method(D_METHOD("get_vis_range"), &SteamAudioProbeVolume::get_vis_range);
	ClassDB::bind_method(D_METHOD("set_vis_range", "p_vis_range"), &SteamAudioProbeVolume::set_vis_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vis_range", PROPERTY_HINT_RANGE, "1.0,200.0,0.5"), "set_vis_range", "get_vis_range");
	ClassDB::bind_method(D_METHOD("get_path_range"), &SteamAudioProbeVolume::get_path_range);
	ClassDB::bind_method(D_METHOD("set_path_range", "p_path_range"), &SteamAudioProbeVolume::set_path_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "path_range", PROPERTY_HINT_RANGE, "1.0,500.0,0.5"), "set_path_range", "get_path_range");
	ClassDB::bind_method(D_METHOD("get_bake_samples"), &SteamAudioProbeVolume::get_bake_samples);
	ClassDB::bind_method(D_METHOD("set_bake_samples", "p_samples"), &SteamAudioProbeVolume::set_bake_samples);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bake_samples", PROPERTY_HINT_RANGE, "1,16,1"), "set_bake_samples", "get_bake_samples");
	ClassDB::bind_method(D_METHOD("get_bake_radius"), &SteamAudioProbeVolume::get_bake_radius);
	ClassDB::bind_method(D_METHOD("set_bake_radius", "p_radius"), &SteamAudioProbeVolume::set_bake_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bake_radius", PROPERTY_HINT_RANGE, "0.1,5.0,0.1"), "set_bake_radius", "get_bake_radius");
	ClassDB::bind_method(D_METHOD("get_bake_threshold"), &SteamAudioProbeVolume::get_bake_threshold);
	ClassDB::bind_method(D_METHOD("set_bake_threshold", "p_threshold"), &SteamAudioProbeVolume::set_bake_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bake_threshold", PROPERTY_HINT_RANGE, "0.0,1.0,0.05"), "set_bake_threshold", "get_bake_threshold");
	ClassDB::bind_method(D_METHOD("get_bake_threads"), &SteamAudioProbeVolume::get_bake_threads);
	ClassDB::bind_method(D_METHOD("set_bake_threads", "p_threads"), &SteamAudioProbeVolume::set_bake_threads);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bake_threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_bake_threads", "get_bake_threads");

	ClassDB::bind_method(D_METHOD("get_baked_data"), &SteamAudioProbeVolume::get_baked_data);
	ClassDB::bind_method(D_METHOD("set_baked_data", "p_data"), &SteamAudioProbeVolume::set_baked_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "baked_data", PROPERTY_HINT_RESOURCE_TYPE, "SteamAudioProbeBatchData"),
			"set_baked_data", "get_baked_data");

	BIND_ENUM_CONSTANT(GENERATION_CENTROID);
	BIND_ENUM_CONSTANT(GENERATION_UNIFORM_FLOOR);
}

SteamAudioProbeVolume::SteamAudioProbeVolume() {
	registered.store(false);
}

SteamAudioProbeVolume::~SteamAudioProbeVolume() {
	unregister_batch();
}

void SteamAudioProbeVolume::set_size(Vector3 p_size) {
	size = Vector3(MAX(p_size.x, 0.1f), MAX(p_size.y, 0.1f), MAX(p_size.z, 0.1f));
}

IPLMatrix4x4 SteamAudioProbeVolume::volume_matrix() const {
	Transform3D trf = get_global_transform();
	Vector3 origin = trf.origin;
	Vector3 scaled = trf.basis.get_scale() * size;
	// Ignore rotation for UNIFORMFLOOR volumes (Phonon volume is an AABB via affine of unit cube).
	return probe_core_volume_matrix(origin.x, origin.y, origin.z, scaled.x, scaled.y, scaled.z);
}

static void add_geometry_to_scene(Node *n, IPLScene scene) {
	if (n == nullptr || scene == nullptr) {
		return;
	}
	if (Object::cast_to<SteamAudioGeometry>(n)) {
		auto *geom = Object::cast_to<SteamAudioGeometry>(n);
		std::vector<IPLStaticMesh> meshes;
		if (Object::cast_to<MeshInstance3D>(geom->get_parent())) {
			meshes = create_meshes_from_mesh_inst_3d(Object::cast_to<MeshInstance3D>(geom->get_parent()), scene, geom->get_material());
		} else if (Object::cast_to<CollisionShape3D>(geom->get_parent())) {
			meshes = create_meshes_from_coll_inst_3d(Object::cast_to<CollisionShape3D>(geom->get_parent()), scene, geom->get_material());
		}
		for (auto m : meshes) {
			if (m) {
				iplStaticMeshAdd(m, scene);
			}
		}
		iplSceneCommit(scene);
	}
	for (int i = 0; i < n->get_child_count(); i++) {
		add_geometry_to_scene(n->get_child(i), scene);
	}
}

void SteamAudioProbeVolume::generate_probes() {
	std::string err;
	IPLContext ctx = probe_core_create_context(&err);
	if (ctx == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		return;
	}

	IPLScene scene = probe_core_create_empty_scene(ctx, &err);
	if (scene == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_destroy_context(&ctx);
		return;
	}

	Node *root = get_tree() ? get_tree()->get_current_scene() : nullptr;
	if (root == nullptr) {
		root = this;
		while (root->get_parent()) {
			root = root->get_parent();
		}
	}
	add_geometry_to_scene(root, scene);

	if (generation == GENERATION_UNIFORM_FLOOR) {
		Vector3 o = get_global_position();
		// Fallback floor at the bottom of the volume so generation still works with no geometry.
		probe_core_add_box(scene, o.x, o.y - size.y * 0.5f, o.z, size.x * 0.5f, 0.05f, size.z * 0.5f, nullptr);
	}

	IPLProbeGenerationType type = generation == GENERATION_CENTROID
			? IPL_PROBEGENERATIONTYPE_CENTROID
			: IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;

	IPLProbeBatch batch = nullptr;
	int count = 0;
	bool ok = probe_core_generate_batch(ctx, scene, type, volume_matrix(), spacing, height, &batch, &count, &err);
	if (!ok) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return;
	}

	std::vector<uint8_t> bytes;
	if (!probe_core_save_batch(ctx, batch, &bytes, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_release_batch(&batch);
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return;
	}

	if (baked_data.is_null()) {
		baked_data.instantiate();
	}
	PackedByteArray arr;
	arr.resize(int(bytes.size()));
	if (!bytes.empty()) {
		memcpy(arr.ptrw(), bytes.data(), bytes.size());
	}
	baked_data->set_bytes(arr);
	baked_data->set_probe_count(count);
	baked_data->set_pathing_baked(false);

	char msg[128];
	snprintf(msg, sizeof(msg), "Generated %d probes", count);
	SteamAudio::log(SteamAudio::log_info, msg);

	probe_core_release_batch(&batch);
	probe_core_destroy_scene(&scene);
	probe_core_destroy_context(&ctx);
}

void SteamAudioProbeVolume::bake_pathing() {
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		generate_probes();
	}
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		SteamAudio::log(SteamAudio::log_error, "bake_pathing: no probes to bake");
		return;
	}

	std::string err;
	IPLContext ctx = probe_core_create_context(&err);
	if (ctx == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		return;
	}

	IPLScene scene = probe_core_create_empty_scene(ctx, &err);
	if (scene == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_destroy_context(&ctx);
		return;
	}
	Node *root = get_tree() ? get_tree()->get_current_scene() : nullptr;
	if (root == nullptr) {
		root = this;
		while (root->get_parent()) {
			root = root->get_parent();
		}
	}
	add_geometry_to_scene(root, scene);
	Vector3 o = get_global_position();
	probe_core_add_box(scene, o.x, o.y - size.y * 0.5f, o.z, size.x * 0.5f, 0.05f, size.z * 0.5f, nullptr);

	PackedByteArray src = baked_data->get_bytes();
	IPLProbeBatch batch = nullptr;
	int count = 0;
	if (!probe_core_load_batch(ctx, src.ptr(), size_t(src.size()), &batch, &count, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return;
	}

	SteamAudio::log(SteamAudio::log_info, "Baking pathing (this may take a while)...");
	if (!probe_core_bake_pathing(ctx, scene, batch, bake_samples, bake_radius, bake_threshold,
				vis_range, path_range, bake_threads, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		probe_core_release_batch(&batch);
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return;
	}

	std::vector<uint8_t> bytes;
	if (!probe_core_save_batch(ctx, batch, &bytes, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
	} else {
		PackedByteArray arr;
		arr.resize(int(bytes.size()));
		if (!bytes.empty()) {
			memcpy(arr.ptrw(), bytes.data(), bytes.size());
		}
		baked_data->set_bytes(arr);
		baked_data->set_probe_count(count);
		baked_data->set_pathing_baked(true);
		char msg[160];
		snprintf(msg, sizeof(msg), "Baked pathing for %d probes (%d bytes)", count, int(bytes.size()));
		SteamAudio::log(SteamAudio::log_info, msg);
	}

	probe_core_release_batch(&batch);
	probe_core_destroy_scene(&scene);
	probe_core_destroy_context(&ctx);
}

int SteamAudioProbeVolume::get_probe_count() const {
	if (baked_data.is_valid()) {
		return baked_data->get_probe_count();
	}
	return 0;
}

void SteamAudioProbeVolume::register_batch() {
	if (registered.load() || baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		return;
	}
	auto srv = SteamAudioServer::get_singleton();
	if (srv == nullptr) {
		return;
	}
	PackedByteArray bytes = baked_data->get_bytes();
	runtime_batch = srv->add_probe_batch(bytes.ptr(), size_t(bytes.size()));
	if (runtime_batch) {
		registered.store(true);
	}
}

void SteamAudioProbeVolume::unregister_batch() {
	if (!registered.load()) {
		runtime_batch = nullptr;
		return;
	}
	auto srv = SteamAudioServer::get_singleton();
	if (srv != nullptr && runtime_batch) {
		srv->remove_probe_batch(runtime_batch);
	}
	runtime_batch = nullptr;
	registered.store(false);
}

void SteamAudioProbeVolume::ready_internal() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	register_batch();
}

void SteamAudioProbeVolume::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			ready_internal();
			break;
		case NOTIFICATION_EXIT_TREE:
			if (!Engine::get_singleton()->is_editor_hint()) {
				unregister_batch();
			}
			break;
	}
}

PackedStringArray SteamAudioProbeVolume::_get_configuration_warnings() const {
	PackedStringArray res;
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		res.push_back("No baked probe data. Call generate_probes() (and bake_pathing() for pathing).");
	} else if (!baked_data->is_pathing_baked()) {
		res.push_back("Probes exist but pathing has not been baked. Call bake_pathing().");
	}
	return res;
}
