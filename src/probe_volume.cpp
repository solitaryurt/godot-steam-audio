#include "probe_volume.hpp"
#include "geometry.hpp"
#include "geometry_common.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "godot_cpp/classes/collision_shape3d.hpp"
#ifdef DEBUG_ENABLED
#include "godot_cpp/classes/editor_file_system.hpp"
#include "godot_cpp/classes/editor_interface.hpp"
#endif
#include "godot_cpp/classes/dir_access.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/mesh_instance3d.hpp"
#include "godot_cpp/classes/resource_saver.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "server.hpp"
#include "steam_audio.hpp"

std::mutex SteamAudioProbeVolume::global_bake_mux;

void SteamAudioProbeBatchData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_bytes"), &SteamAudioProbeBatchData::get_bytes);
	ClassDB::bind_method(D_METHOD("set_bytes", "p_bytes"), &SteamAudioProbeBatchData::set_bytes);
	ClassDB::bind_method(D_METHOD("get_probe_positions"), &SteamAudioProbeBatchData::get_probe_positions);
	ClassDB::bind_method(D_METHOD("set_probe_positions", "p_positions"), &SteamAudioProbeBatchData::set_probe_positions);
	ClassDB::bind_method(D_METHOD("get_probe_radii"), &SteamAudioProbeBatchData::get_probe_radii);
	ClassDB::bind_method(D_METHOD("set_probe_radii", "p_radii"), &SteamAudioProbeBatchData::set_probe_radii);
	ClassDB::bind_method(D_METHOD("get_probe_count"), &SteamAudioProbeBatchData::get_probe_count);
	ClassDB::bind_method(D_METHOD("set_probe_count", "p_count"), &SteamAudioProbeBatchData::set_probe_count);
	ClassDB::bind_method(D_METHOD("is_pathing_baked"), &SteamAudioProbeBatchData::is_pathing_baked);
	ClassDB::bind_method(D_METHOD("set_pathing_baked", "p_baked"), &SteamAudioProbeBatchData::set_pathing_baked);
	ClassDB::bind_method(D_METHOD("are_reflections_baked"), &SteamAudioProbeBatchData::are_reflections_baked);
	ClassDB::bind_method(D_METHOD("set_reflections_baked", "p_baked"), &SteamAudioProbeBatchData::set_reflections_baked);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"), "set_bytes", "get_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "probe_positions", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
			"set_probe_positions", "get_probe_positions");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "probe_radii", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE),
			"set_probe_radii", "get_probe_radii");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "probe_count"), "set_probe_count", "get_probe_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pathing_baked"), "set_pathing_baked", "is_pathing_baked");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reflections_baked"), "set_reflections_baked", "are_reflections_baked");
}

void SteamAudioProbeVolume::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate_probes"), &SteamAudioProbeVolume::generate_probes);
	ClassDB::bind_method(D_METHOD("bake_pathing"), &SteamAudioProbeVolume::bake_pathing);
	ClassDB::bind_method(D_METHOD("bake_reflections"), &SteamAudioProbeVolume::bake_reflections);
	ClassDB::bind_method(D_METHOD("cancel_bake"), &SteamAudioProbeVolume::cancel_bake);
	ClassDB::bind_method(D_METHOD("save_baked_data"), &SteamAudioProbeVolume::save_baked_data);
	ClassDB::bind_method(D_METHOD("get_probe_count"), &SteamAudioProbeVolume::get_probe_count);
	ClassDB::bind_method(D_METHOD("is_baking"), &SteamAudioProbeVolume::is_baking);
	ClassDB::bind_method(D_METHOD("is_baking_reflections"), &SteamAudioProbeVolume::is_baking_reflections);
	ClassDB::bind_method(D_METHOD("get_bake_progress"), &SteamAudioProbeVolume::get_bake_progress);
	ClassDB::bind_method(D_METHOD("get_probe_positions"), &SteamAudioProbeVolume::get_probe_positions);

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

	ADD_GROUP("Reflection Bake", "reflection_");
	ClassDB::bind_method(D_METHOD("get_reflection_rays"), &SteamAudioProbeVolume::get_reflection_rays);
	ClassDB::bind_method(D_METHOD("set_reflection_rays", "p_value"), &SteamAudioProbeVolume::set_reflection_rays);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_rays", PROPERTY_HINT_RANGE, "32,65536,32"), "set_reflection_rays", "get_reflection_rays");
	ClassDB::bind_method(D_METHOD("get_reflection_diffuse_samples"), &SteamAudioProbeVolume::get_reflection_diffuse_samples);
	ClassDB::bind_method(D_METHOD("set_reflection_diffuse_samples", "p_value"), &SteamAudioProbeVolume::set_reflection_diffuse_samples);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_diffuse_samples", PROPERTY_HINT_RANGE, "1,4096,1"), "set_reflection_diffuse_samples", "get_reflection_diffuse_samples");
	ClassDB::bind_method(D_METHOD("get_reflection_bounces"), &SteamAudioProbeVolume::get_reflection_bounces);
	ClassDB::bind_method(D_METHOD("set_reflection_bounces", "p_value"), &SteamAudioProbeVolume::set_reflection_bounces);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_bounces", PROPERTY_HINT_RANGE, "1,64,1"), "set_reflection_bounces", "get_reflection_bounces");
	ClassDB::bind_method(D_METHOD("get_reflection_simulated_duration"), &SteamAudioProbeVolume::get_reflection_simulated_duration);
	ClassDB::bind_method(D_METHOD("set_reflection_simulated_duration", "p_value"), &SteamAudioProbeVolume::set_reflection_simulated_duration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_simulated_duration", PROPERTY_HINT_RANGE, "0.1,10.0,0.1"), "set_reflection_simulated_duration", "get_reflection_simulated_duration");
	ClassDB::bind_method(D_METHOD("get_reflection_saved_duration"), &SteamAudioProbeVolume::get_reflection_saved_duration);
	ClassDB::bind_method(D_METHOD("set_reflection_saved_duration", "p_value"), &SteamAudioProbeVolume::set_reflection_saved_duration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_saved_duration", PROPERTY_HINT_RANGE, "0.1,10.0,0.1"), "set_reflection_saved_duration", "get_reflection_saved_duration");
	ClassDB::bind_method(D_METHOD("get_reflection_order"), &SteamAudioProbeVolume::get_reflection_order);
	ClassDB::bind_method(D_METHOD("set_reflection_order", "p_value"), &SteamAudioProbeVolume::set_reflection_order);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_order", PROPERTY_HINT_RANGE, "0,5,1"), "set_reflection_order", "get_reflection_order");
	ClassDB::bind_method(D_METHOD("get_reflection_threads"), &SteamAudioProbeVolume::get_reflection_threads);
	ClassDB::bind_method(D_METHOD("set_reflection_threads", "p_value"), &SteamAudioProbeVolume::set_reflection_threads);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_reflection_threads", "get_reflection_threads");
	ClassDB::bind_method(D_METHOD("get_reflection_irradiance_min_distance"), &SteamAudioProbeVolume::get_reflection_irradiance_min_distance);
	ClassDB::bind_method(D_METHOD("set_reflection_irradiance_min_distance", "p_value"), &SteamAudioProbeVolume::set_reflection_irradiance_min_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_irradiance_min_distance", PROPERTY_HINT_RANGE, "0.1,10.0,0.1"), "set_reflection_irradiance_min_distance", "get_reflection_irradiance_min_distance");

	ClassDB::bind_method(D_METHOD("get_save_path"), &SteamAudioProbeVolume::get_save_path);
	ClassDB::bind_method(D_METHOD("set_save_path", "p_path"), &SteamAudioProbeVolume::set_save_path);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_path", PROPERTY_HINT_SAVE_FILE, "*.res,*.tres"),
			"set_save_path", "get_save_path");

	ClassDB::bind_method(D_METHOD("get_baked_data"), &SteamAudioProbeVolume::get_baked_data);
	ClassDB::bind_method(D_METHOD("set_baked_data", "p_data"), &SteamAudioProbeVolume::set_baked_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "baked_data", PROPERTY_HINT_RESOURCE_TYPE, "SteamAudioProbeBatchData"),
			"set_baked_data", "get_baked_data");

	ADD_SIGNAL(MethodInfo("probes_generated"));
	ADD_SIGNAL(MethodInfo("bake_started"));
	ADD_SIGNAL(MethodInfo("bake_finished", PropertyInfo(Variant::BOOL, "success")));

	BIND_ENUM_CONSTANT(GENERATION_CENTROID);
	BIND_ENUM_CONSTANT(GENERATION_UNIFORM_FLOOR);
}

SteamAudioProbeVolume::SteamAudioProbeVolume() {
	registered.store(false);
}

SteamAudioProbeVolume::~SteamAudioProbeVolume() {
	cancel_bake();
	join_bake_thread();
	release_bake_job();
	unregister_batch();
}

void SteamAudioProbeVolume::set_size(Vector3 p_size) {
	size = Vector3(MAX(p_size.x, 0.1f), MAX(p_size.y, 0.1f), MAX(p_size.z, 0.1f));
	refresh_editor_visuals();
}

void SteamAudioProbeVolume::set_baked_data(Ref<SteamAudioProbeBatchData> p_data) {
	if (is_baking()) {
		SteamAudio::log(SteamAudio::log_warn, "set_baked_data: cannot replace data until the bake is finalized");
		return;
	}
	baked_data = p_data;
	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		unregister_batch();
		register_batch();
	}
	refresh_editor_visuals();
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

static Node *find_scene_root(SteamAudioProbeVolume *self) {
	Node *root = self->get_tree() ? self->get_tree()->get_current_scene() : nullptr;
#ifdef DEBUG_ENABLED
	if (root == nullptr && Engine::get_singleton()->is_editor_hint()) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei) {
			root = ei->get_edited_scene_root();
		}
	}
#endif
	if (root == nullptr) {
		root = self;
		while (root->get_parent()) {
			root = root->get_parent();
		}
	}
	return root;
}

void SteamAudioProbeVolume::apply_serialized_batch(const std::vector<uint8_t> &bytes, int count, bool pathing,
		const std::vector<IPLVector3> *positions, const std::vector<float> *radii) {
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
	baked_data->set_pathing_baked(pathing);
	if (positions) {
		PackedVector3Array pos;
		pos.resize(int(positions->size()));
		Vector3 *w = pos.ptrw();
		for (int i = 0; i < int(positions->size()); i++) {
			const IPLVector3 &p = (*positions)[size_t(i)];
			w[i] = Vector3(p.x, p.y, p.z);
		}
		baked_data->set_probe_positions(pos);
	}
	if (radii) {
		PackedFloat32Array packed_radii;
		packed_radii.resize(int(radii->size()));
		float *w = packed_radii.ptrw();
		for (int i = 0; i < int(radii->size()); i++) {
			w[i] = (*radii)[size_t(i)];
		}
		baked_data->set_probe_radii(packed_radii);
	}
}

void SteamAudioProbeVolume::refresh_editor_visuals() {
	if (!is_inside_tree()) {
		return;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		update_gizmos();
		// This emits a SceneTree signal; defer it past our callers' final callbacks.
		call_deferred("update_configuration_warnings");
	}
}

PackedVector3Array SteamAudioProbeVolume::get_probe_positions() const {
	if (baked_data.is_valid()) {
		return baked_data->get_probe_positions();
	}
	return PackedVector3Array();
}

String SteamAudioProbeVolume::resolve_save_path() {
	if (!save_path.is_empty()) {
		return save_path;
	}
	if (baked_data.is_valid() && !baked_data->get_path().is_empty() &&
			!baked_data->get_path().contains("::")) {
		return baked_data->get_path();
	}

	String scene_path;
	Node *owner = get_owner();
	if (owner) {
		scene_path = owner->get_scene_file_path();
	}
#ifdef DEBUG_ENABLED
	if (scene_path.is_empty() && Engine::get_singleton()->is_editor_hint()) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei && ei->get_edited_scene_root()) {
			scene_path = ei->get_edited_scene_root()->get_scene_file_path();
		}
	}
#endif

	String base;
	String stem = String(get_name()).validate_filename();
	if (stem.is_empty()) {
		stem = "probe_volume";
	}
	if (scene_path.is_empty()) {
		base = "res://steam_audio_bakes";
		stem = stem + "_" + String::num_uint64(int64_t(get_instance_id()));
	} else {
		base = scene_path.get_base_dir();
		String node_key = owner ? String(owner->get_path_to(this)) : String(get_name());
		node_key = node_key.uri_encode().validate_filename();
		stem = scene_path.get_file().get_basename() + "_" + node_key;
	}
	return base.path_join(stem + ".res");
}

Error SteamAudioProbeVolume::save_baked_data() {
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		SteamAudio::log(SteamAudio::log_error, "save_baked_data: nothing to save");
		return ERR_UNCONFIGURED;
	}
	String path = resolve_save_path();
	if (path.contains("::") || (path.get_extension().to_lower() != "res" && path.get_extension().to_lower() != "tres")) {
		SteamAudio::log(SteamAudio::log_error, "save_baked_data: choose a standalone .res or .tres file, not an embedded resource path");
		return ERR_INVALID_PARAMETER;
	}
	bool scene_binding_changed = save_path != path || baked_data->get_path() != path;
	Error dir_err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (dir_err != OK && dir_err != ERR_ALREADY_EXISTS) {
		char msg[256];
		snprintf(msg, sizeof(msg), "save_baked_data: could not create %s", path.get_base_dir().utf8().get_data());
		SteamAudio::log(SteamAudio::log_error, msg);
		return dir_err;
	}
	Ref<SteamAudioProbeBatchData> data = baked_data;
	const auto instance_id = get_instance_id();
	Error err = ResourceSaver::get_singleton()->save(data, path, ResourceSaver::FLAG_COMPRESS);
	if (err != OK) {
		char msg[256];
		snprintf(msg, sizeof(msg), "save_baked_data: failed to write %s", path.utf8().get_data());
		SteamAudio::log(SteamAudio::log_error, msg);
		return err;
	}
	if (ObjectDB::get_instance(instance_id) != this) {
		return OK;
	}
	if (baked_data != data) {
		return ERR_BUSY;
	}
	data->take_over_path(path);
	if (ObjectDB::get_instance(instance_id) != this) {
		return OK;
	}
	save_path = path;
#ifdef DEBUG_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei) {
			if (ei->get_resource_filesystem()) {
				ei->get_resource_filesystem()->update_file(path);
			}
			if (scene_binding_changed) {
				ei->mark_scene_as_unsaved();
			}
		}
	}
#endif
	if (ObjectDB::get_instance(instance_id) != this) {
		return OK;
	}
	call_deferred("notify_property_list_changed");
	char msg[256];
	snprintf(msg, sizeof(msg), "Saved probe bake to %s", path.utf8().get_data());
	SteamAudio::log(SteamAudio::log_info, msg);
	return OK;
}

void SteamAudioProbeVolume::generate_probes() {
	if (is_baking()) {
		SteamAudio::log(SteamAudio::log_warn, "generate_probes: bake in progress");
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

	add_geometry_to_scene(find_scene_root(this), scene);

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
	std::vector<IPLVector3> positions;
	std::vector<float> radii;
	bool ok = probe_core_generate_batch(ctx, scene, type, volume_matrix(), spacing, height,
			&batch, &count, &err, &positions, &radii);
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

	char msg[128];
	snprintf(msg, sizeof(msg), "Generated %d probes", count);
	SteamAudio::log(SteamAudio::log_info, msg);

	probe_core_release_batch(&batch);
	probe_core_destroy_scene(&scene);
	probe_core_destroy_context(&ctx);

	apply_serialized_batch(bytes, count, false, &positions, &radii);
	baked_data->set_reflections_baked(false);
	Ref<SteamAudioProbeBatchData> data = baked_data;
	const auto instance_id = get_instance_id();
	if (Engine::get_singleton()->is_editor_hint()) {
		save_baked_data();
		if (ObjectDB::get_instance(instance_id) != this) {
			return;
		}
	}

	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		unregister_batch();
		register_batch();
	}

	refresh_editor_visuals();
	data->emit_changed();
	if (ObjectDB::get_instance(instance_id) != this) {
		return;
	}
	emit_signal("probes_generated");
}

static bool has_probe_metadata(const Ref<SteamAudioProbeBatchData> &data, int count) {
	PackedVector3Array positions = data->get_probe_positions();
	PackedFloat32Array radii = data->get_probe_radii();
	if (count <= 0 || positions.size() != count || radii.size() != count) {
		return false;
	}
	for (int i = 0; i < count; i++) {
		if (!positions[i].is_finite() || !std::isfinite(radii[i]) || radii[i] <= 0.f) {
			return false;
		}
	}
	return true;
}

void SteamAudioProbeVolume::start_bake(BakeKind p_kind) {
	if (is_baking()) {
		return;
	}

	const bool editor = Engine::get_singleton()->is_editor_hint();
	const auto instance_id = get_instance_id();
	const uint64_t previous_bake = bake_sequence;
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		generate_probes();
	}
	// A generation handler may even have completed a blocking bake before returning.
	if (ObjectDB::get_instance(instance_id) != this) {
		return;
	}
	if (is_baking() || bake_sequence != previous_bake || (editor && !is_inside_tree())) {
		return;
	}
	if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		SteamAudio::log(SteamAudio::log_error, "bake: no probes to bake");
		return;
	}
	if (!global_bake_mux.try_lock()) {
		SteamAudio::log(SteamAudio::log_warn, "Another Steam Audio bake is already running");
		return;
	}
	bake_lock_held = true;
	bake_kind = p_kind;
	// Resource properties can be edited directly, even when reassignment is rejected.
	// Preserve the metadata belonging to the serialized batch we actually bake.
	bake_data.instantiate();
	bake_data->set_bytes(baked_data->get_bytes());
	bake_data->set_probe_positions(baked_data->get_probe_positions());
	bake_data->set_probe_radii(baked_data->get_probe_radii());

	std::string err;
	bake_ctx = probe_core_create_context(&err);
	if (bake_ctx == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		release_bake_job();
		return;
	}
	bake_scene = probe_core_create_empty_scene(bake_ctx, &err);
	if (bake_scene == nullptr) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		release_bake_job();
		return;
	}
	add_geometry_to_scene(find_scene_root(this), bake_scene);
	if (bake_kind == BAKE_PATHING) {
		Vector3 o = get_global_position();
		probe_core_add_box(bake_scene, o.x, o.y - size.y * 0.5f, o.z, size.x * 0.5f, 0.05f, size.z * 0.5f, nullptr);
	}

	PackedByteArray src = bake_data->get_bytes();
	if (!probe_core_load_batch(bake_ctx, src.ptr(), size_t(src.size()), &bake_batch, &bake_count, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		release_bake_job();
		return;
	}
	// The public SDK can count loaded probes, but cannot read their spheres back.
	if (!has_probe_metadata(bake_data, bake_count)) {
		release_bake_job();
		SteamAudio::log(SteamAudio::log_error, "bake: probe positions/radii are missing or invalid; explicitly Generate Probes again before baking");
		return;
	}
	bake_data->set_pathing_baked(probe_core_pathing_data_size(bake_batch) > 0);
	bake_data->set_reflections_baked(probe_core_reflections_data_size(bake_batch) > 0);

	bake_bytes.clear();
	bake_error.clear();
	bake_settings = { bake_samples, bake_radius, bake_threshold, vis_range, path_range, bake_threads,
		reflection_rays, reflection_diffuse_samples, reflection_bounces, reflection_simulated_duration,
		reflection_saved_duration, reflection_order, reflection_threads, reflection_irradiance_min_distance };
	bake_progress.store(0.f);
	bake_cancel_requested.store(false);
	++bake_sequence;
	bake_state.store(BAKE_RUNNING);
	SteamAudio::log(SteamAudio::log_info, bake_kind == BAKE_REFLECTIONS ? "Baking reflections..." : "Baking pathing...");
	if (editor) {
		set_process_internal(true);
		bake_thread = std::thread(&SteamAudioProbeVolume::bake_worker, this);
		// Nothing may touch the job after this callback: it can remove/free the node.
		emit_signal("bake_started");
	} else {
		bake_worker();
		finish_bake();
	}
}

void IPLCALL SteamAudioProbeVolume::bake_progress_cb(IPLfloat32 progress, void *user) {
	auto *self = static_cast<SteamAudioProbeVolume *>(user);
	if (self) {
		// SDK 4.5 path visibility reports 0/0 for a single-probe batch.
		if (std::isfinite(progress)) {
			self->bake_progress.store(CLAMP(progress, 0.f, 1.f));
		}
		// SDK cancellation before its bake entry point is ignored. Deliver the
		// persistent request from inside the bake, including during exit-tree joins.
		if (self->bake_cancel_requested.load()) {
			if (self->bake_kind == BAKE_REFLECTIONS) {
				probe_core_cancel_reflections_bake(self->bake_ctx);
			} else {
				probe_core_cancel_pathing_bake(self->bake_ctx);
			}
		}
	}
}

void SteamAudioProbeVolume::bake_worker() {
	if (bake_cancel_requested.load()) {
		bake_state.store(BAKE_DONE_CANCEL);
		return;
	}
	std::string err;
	bool ok;
	if (bake_kind == BAKE_REFLECTIONS) {
		ok = probe_core_bake_reflections(bake_ctx, bake_scene, bake_batch,
				bake_settings.reflection_rays, bake_settings.reflection_diffuse_samples,
				bake_settings.reflection_bounces, bake_settings.reflection_simulated_duration,
				bake_settings.reflection_saved_duration, bake_settings.reflection_order,
				bake_settings.reflection_threads, bake_settings.reflection_irradiance_min_distance,
				1, &err, bake_progress_cb, this);
	} else {
		ok = probe_core_bake_pathing(bake_ctx, bake_scene, bake_batch,
				bake_settings.path_samples, bake_settings.path_radius, bake_settings.path_threshold,
				bake_settings.path_vis_range, bake_settings.path_range, bake_settings.path_threads,
				&err, bake_progress_cb, this);
	}
	if (bake_cancel_requested.load()) {
		bake_state.store(BAKE_DONE_CANCEL);
		return;
	}
	if (!ok) {
		bake_error = err;
		bake_state.store(BAKE_DONE_FAIL);
		return;
	}
	std::vector<uint8_t> bytes;
	if (!probe_core_save_batch(bake_ctx, bake_batch, &bytes, &err)) {
		bake_error = err;
		bake_state.store(BAKE_DONE_FAIL);
		return;
	}
	bake_bytes = std::move(bytes);
	bake_progress.store(1.f);
	bake_state.store(bake_cancel_requested.load() ? BAKE_DONE_CANCEL : BAKE_DONE_OK);
}

void SteamAudioProbeVolume::cancel_bake() {
	const int state = bake_state.load();
	if (state != BAKE_IDLE && state != BAKE_FINALIZING) {
		bake_cancel_requested.store(true);
	}
}

void SteamAudioProbeVolume::join_bake_thread() {
	if (bake_thread.joinable()) {
		bake_thread.join();
	}
}

void SteamAudioProbeVolume::release_bake_job() {
	probe_core_release_batch(&bake_batch);
	probe_core_destroy_scene(&bake_scene);
	probe_core_destroy_context(&bake_ctx);
	bake_bytes.clear();
	bake_data.unref();
	bake_count = 0;
	if (bake_lock_held) {
		global_bake_mux.unlock();
		bake_lock_held = false;
	}
}

void SteamAudioProbeVolume::finish_bake() {
	join_bake_thread();
	int st = bake_state.exchange(BAKE_FINALIZING);
	set_process_internal(false);

	bool success = false;
	const bool editor = Engine::get_singleton()->is_editor_hint();
	const auto instance_id = get_instance_id();
	if (bake_cancel_requested.load()) {
		st = BAKE_DONE_CANCEL;
	}
	if (st == BAKE_DONE_OK) {
		apply_serialized_batch(bake_bytes, bake_count,
				bake_kind == BAKE_PATHING || bake_data->is_pathing_baked(), nullptr);
		baked_data->set_reflections_baked(bake_kind == BAKE_REFLECTIONS || bake_data->are_reflections_baked());
		baked_data->set_probe_positions(bake_data->get_probe_positions());
		baked_data->set_probe_radii(bake_data->get_probe_radii());
	}
	// Release all SDK objects and global ownership before save/resource callbacks.
	// Stay busy until those callbacks finish so this job cannot finalize twice.
	release_bake_job();
	if (st == BAKE_DONE_OK) {
		Error save_error = editor ? save_baked_data() : OK;
		if (ObjectDB::get_instance(instance_id) != this) {
			return;
		}
		if (save_error != OK) {
			SteamAudio::log(SteamAudio::log_error, "Bake completed but could not be saved");
		} else {
			success = true;
		}
		if (!editor && is_inside_tree()) {
			unregister_batch();
			register_batch();
		}
	} else if (st == BAKE_DONE_CANCEL) {
		SteamAudio::log(SteamAudio::log_info, "Bake cancelled");
	} else if (st == BAKE_DONE_FAIL) {
		SteamAudio::log(SteamAudio::log_error, bake_error.c_str());
	}

	if (st == BAKE_DONE_OK) {
		Ref<SteamAudioProbeBatchData> data = baked_data;
		data->emit_changed();
		if (ObjectDB::get_instance(instance_id) != this) {
			return;
		}
	}
	bake_state.store(BAKE_IDLE);
	refresh_editor_visuals();
	if (editor) {
		emit_signal("bake_finished", success);
	}
}

void SteamAudioProbeVolume::bake_pathing() {
	if (is_baking()) {
		SteamAudio::log(SteamAudio::log_warn, "bake_pathing: already running");
		return;
	}
	start_bake(BAKE_PATHING);
}

void SteamAudioProbeVolume::bake_reflections() {
	if (is_baking()) {
		SteamAudio::log(SteamAudio::log_warn, "bake_reflections: already running");
		return;
	}
	start_bake(BAKE_REFLECTIONS);
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
	if (!baked_data->is_pathing_baked() && !baked_data->are_reflections_baked()) {
		return;
	}
	if (baked_data->are_reflections_baked() && !has_probe_metadata(baked_data, baked_data->get_probe_count())) {
		SteamAudio::log(SteamAudio::log_error, "register_batch: reflection probe metadata is missing or invalid; explicitly Generate Probes and bake again");
		return;
	}
	auto srv = SteamAudioServer::get_singleton();
	if (srv == nullptr) {
		return;
	}
	PackedByteArray bytes = baked_data->get_bytes();
	PackedVector3Array positions = baked_data->get_probe_positions();
	PackedFloat32Array radii = baked_data->get_probe_radii();
	std::vector<IPLSphere> probes;
	probes.reserve(size_t(MIN(positions.size(), radii.size())));
	for (int i = 0; i < positions.size() && i < radii.size(); i++) {
		probes.push_back({ ipl_vec3_from(positions[i]), radii[i] });
	}
	runtime_batch = srv->add_probe_batch(bytes.ptr(), size_t(bytes.size()),
			baked_data->is_pathing_baked(), baked_data->are_reflections_baked(), probes);
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
			set_process_internal(false);
			if (bake_state.load() != BAKE_FINALIZING) {
				cancel_bake();
				join_bake_thread();
				release_bake_job();
				bake_state.store(BAKE_IDLE);
			}
			if (!Engine::get_singleton()->is_editor_hint()) {
				unregister_batch();
			}
			break;
		case NOTIFICATION_INTERNAL_PROCESS:
			if (bake_state.load() >= BAKE_DONE_OK && bake_state.load() <= BAKE_DONE_CANCEL) {
				finish_bake();
			}
			break;
	}
}

PackedStringArray SteamAudioProbeVolume::_get_configuration_warnings() const {
	PackedStringArray res;
	if (is_baking()) {
		res.push_back(is_baking_reflections() ? "Reflection bake in progress." : "Pathing bake in progress.");
	} else if (baked_data.is_null() || baked_data->get_bytes().is_empty()) {
		res.push_back("No baked probe data. Use Generate Probes, then Bake Pathing.");
	} else if (!has_probe_metadata(baked_data, baked_data->get_probe_count())) {
		res.push_back("Probe positions/radii are missing or invalid. Explicitly Generate Probes again before baking.");
	} else if (!baked_data->is_pathing_baked()) {
		res.push_back("Probes exist but pathing has not been baked. Use Bake Pathing.");
	}
	return res;
}
