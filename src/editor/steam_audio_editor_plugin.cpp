#include "steam_audio_editor_plugin.hpp"

#include "godot_cpp/classes/editor_node3d_gizmo.hpp"
#include "godot_cpp/classes/editor_interface.hpp"
#include "godot_cpp/classes/editor_undo_redo_manager.hpp"
#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/standard_material3d.hpp"
#include "godot_cpp/classes/text_server.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/packed_vector3_array.hpp"
#include "godot_cpp/variant/variant.hpp"

void SteamAudioProbeBakePanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_generate"), &SteamAudioProbeBakePanel::_on_generate);
	ClassDB::bind_method(D_METHOD("_on_bake_pathing"), &SteamAudioProbeBakePanel::_on_bake_pathing);
	ClassDB::bind_method(D_METHOD("_on_bake_reflections"), &SteamAudioProbeBakePanel::_on_bake_reflections);
	ClassDB::bind_method(D_METHOD("_on_cancel"), &SteamAudioProbeBakePanel::_on_cancel);
}

SteamAudioProbeVolume *SteamAudioProbeBakePanel::volume() const {
	return Object::cast_to<SteamAudioProbeVolume>(ObjectDB::get_instance(volume_id));
}

void SteamAudioProbeBakePanel::setup(SteamAudioProbeVolume *p_volume) {
	if (p_volume == nullptr) {
		return;
	}
	volume_id = p_volume->get_instance_id();

	Label *header = memnew(Label);
	header->set_text("Steam Audio Baking");
	add_child(header);

	generate_btn = memnew(Button);
	generate_btn->set_text("Generate Probes");
	generate_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	generate_btn->set_tooltip_text("Place probes in this volume. Saved to its own .res file.");
	generate_btn->connect("pressed", Callable(this, "_on_generate"));
	add_child(generate_btn);

	pathing_btn = memnew(Button);
	pathing_btn->set_text("Bake Pathing");
	pathing_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	pathing_btn->set_tooltip_text("Bake probe-to-probe paths.");
	pathing_btn->connect("pressed", Callable(this, "_on_bake_pathing"));
	add_child(pathing_btn);

	reflections_btn = memnew(Button);
	reflections_btn->set_text("Bake Reflections");
	reflections_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	reflections_btn->set_tooltip_text("Bake listener-centric convolution reverb at each probe.");
	reflections_btn->connect("pressed", Callable(this, "_on_bake_reflections"));
	add_child(reflections_btn);

	cancel_btn = memnew(Button);
	cancel_btn->set_text("Cancel");
	cancel_btn->set_tooltip_text("Discard the bake result. Pathing finishes in the background before its result is discarded; native pathing cancellation is not used.");
	cancel_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	cancel_btn->connect("pressed", Callable(this, "_on_cancel"));
	add_child(cancel_btn);

	progress = memnew(ProgressBar);
	progress->set_min(0);
	progress->set_max(1);
	progress->set_step(0.01);
	progress->set_show_percentage(true);
	add_child(progress);

	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(status);

	set_process(true);
	refresh();
}

void SteamAudioProbeBakePanel::refresh() {
	SteamAudioProbeVolume *vol = volume();
	if (vol == nullptr) {
		return;
	}
	const bool baking = vol->is_baking();
	if (generate_btn) {
		generate_btn->set_disabled(baking);
	}
	if (pathing_btn) {
		pathing_btn->set_disabled(baking);
	}
	if (reflections_btn) {
		reflections_btn->set_disabled(baking);
	}
	if (cancel_btn) {
		cancel_btn->set_disabled(!baking || vol->is_bake_cancelling());
	}
	if (progress) {
		progress->set_visible(baking);
		progress->set_value(vol->get_bake_progress());
	}
	if (status == nullptr) {
		return;
	}
	if (baking) {
		if (vol->is_bake_cancelling()) {
			status->set_text("Cancellation requested. Waiting for the bake worker; the result will be discarded.");
			return;
		}
		status->set_text(vformat("Baking %s... %.0f%%",
				vol->is_baking_reflections() ? "reflections" : "pathing", vol->get_bake_progress() * 100.0));
		return;
	}
	const int n = vol->get_probe_count();
	Ref<SteamAudioProbeBatchData> data = vol->get_baked_data();
	String path = vol->get_save_path();
	if (data.is_valid() && !data->get_path().is_empty()) {
		path = data->get_path();
	}
	if (n <= 0) {
		status->set_text("No probes yet.");
	} else {
		String layers;
		if (data.is_valid() && data->is_pathing_baked()) {
			layers += "pathing";
		}
		if (data.is_valid() && data->are_reflections_baked()) {
			layers += layers.is_empty() ? "reflections" : ", reflections";
		}
		if (layers.is_empty()) {
			layers = "no baked effects";
		}
		status->set_text(vformat("%d probes (%s)%s", n, layers,
				path.is_empty() ? String() : String("\n") + path));
	}
}

void SteamAudioProbeBakePanel::_process(double p_delta) {
	(void)p_delta;
	refresh();
}

void SteamAudioProbeBakePanel::_on_generate() {
	SteamAudioProbeVolume *vol = volume();
	if (vol) {
		vol->generate_probes();
		refresh();
	}
}

void SteamAudioProbeBakePanel::_on_bake_pathing() {
	SteamAudioProbeVolume *vol = volume();
	if (vol) {
		vol->bake_pathing();
		refresh();
	}
}

void SteamAudioProbeBakePanel::_on_bake_reflections() {
	SteamAudioProbeVolume *vol = volume();
	if (vol) {
		vol->bake_reflections();
		refresh();
	}
}

void SteamAudioProbeBakePanel::_on_cancel() {
	SteamAudioProbeVolume *vol = volume();
	if (vol) {
		vol->cancel_bake();
		refresh();
	}
}

bool SteamAudioProbeVolumeInspectorPlugin::_can_handle(Object *p_object) const {
	return Object::cast_to<SteamAudioProbeVolume>(p_object) != nullptr;
}

void SteamAudioProbeVolumeInspectorPlugin::_parse_begin(Object *p_object) {
	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_object);
	if (vol == nullptr) {
		return;
	}
	SteamAudioProbeBakePanel *panel = memnew(SteamAudioProbeBakePanel);
	panel->setup(vol);
	add_custom_control(panel);
}

void SteamAudioProbeVolumeGizmoPlugin::ensure_materials() {
	if (materials_ready) {
		return;
	}
	create_material("volume", Color(0.25f, 0.7f, 1.0f), false, true);
	create_material("probes", Color(1.0f, 0.55f, 0.15f), false, true);
	create_material("probes_baked", Color(0.2f, 0.95f, 0.45f), false, true);
	create_handle_material("handles");
	materials_ready = true;
}

String SteamAudioProbeVolumeGizmoPlugin::_get_handle_name(const Ref<EditorNode3DGizmo> &p_gizmo,
		int32_t p_handle_id, bool p_secondary) const {
	(void)p_gizmo;
	(void)p_secondary;
	static const char *names[] = { "Size X+", "Size X-", "Size Y+", "Size Y-", "Size Z+", "Size Z-" };
	return p_handle_id >= 0 && p_handle_id < 6 ? names[p_handle_id] : "";
}

Variant SteamAudioProbeVolumeGizmoPlugin::_get_handle_value(const Ref<EditorNode3DGizmo> &p_gizmo,
		int32_t p_handle_id, bool p_secondary) const {
	(void)p_handle_id;
	(void)p_secondary;
	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_gizmo->get_node_3d());
	return vol ? Variant(AABB(vol->get_global_position(), vol->get_size())) : Variant();
}

void SteamAudioProbeVolumeGizmoPlugin::_begin_handle_action(const Ref<EditorNode3DGizmo> &p_gizmo,
		int32_t p_handle_id, bool p_secondary) {
	(void)p_handle_id;
	(void)p_secondary;
	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_gizmo->get_node_3d());
	if (!vol) {
		return;
	}
	handle_start_position = vol->get_global_position();
	handle_start_size = vol->get_size();
	handle_start_scale = vol->get_global_transform().basis.get_scale().abs();
}

void SteamAudioProbeVolumeGizmoPlugin::_set_handle(const Ref<EditorNode3DGizmo> &p_gizmo,
		int32_t p_handle_id, bool p_secondary, Camera3D *p_camera, const Vector2 &p_screen_pos) {
	(void)p_secondary;
	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_gizmo->get_node_3d());
	if (!vol || !p_camera || p_handle_id < 0 || p_handle_id >= 6) {
		return;
	}

	const int axis_index = p_handle_id / 2;
	const float sign = (p_handle_id % 2 == 0) ? 1.0f : -1.0f;
	Vector3 axis;
	axis[axis_index] = 1.0f;
	const Vector3 ray_origin = p_camera->project_ray_origin(p_screen_pos);
	const Vector3 ray_direction = p_camera->project_ray_normal(p_screen_pos).normalized();
	const Vector3 between = handle_start_position - ray_origin;
	const float parallel = axis.dot(ray_direction);
	const float denominator = 1.0f - parallel * parallel;
	if (Math::abs(denominator) < 0.00001f) {
		return;
	}
	const float axis_offset = axis.dot(between);
	const float ray_offset = ray_direction.dot(between);
	float ray_distance = (ray_offset - parallel * axis_offset) / denominator;
	float axis_distance;
	if (ray_distance < 0.0f) {
		axis_distance = -axis_offset;
	} else if (ray_distance > 16384.0f) {
		axis_distance = -axis_offset + parallel * 16384.0f;
	} else {
		axis_distance = (parallel * ray_offset - axis_offset) / denominator;
	}
	float face = handle_start_position[axis_index] + axis_distance;

	const float scale = MAX(handle_start_scale[axis_index], 0.00001f);
	const float half_extent = handle_start_size[axis_index] * scale * 0.5f;
	const float opposite_face = handle_start_position[axis_index] - sign * half_extent;
	const float minimum_extent = 0.1f * scale;
	if (sign * (face - opposite_face) < minimum_extent) {
		face = opposite_face + sign * minimum_extent;
	}

	Vector3 size = handle_start_size;
	Vector3 position = handle_start_position;
	size[axis_index] = sign * (face - opposite_face) / scale;
	position[axis_index] = (face + opposite_face) * 0.5f;
	vol->set_size(size);
	vol->set_global_position(position);
}

void SteamAudioProbeVolumeGizmoPlugin::_commit_handle(const Ref<EditorNode3DGizmo> &p_gizmo,
		int32_t p_handle_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	(void)p_handle_id;
	(void)p_secondary;
	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_gizmo->get_node_3d());
	if (!vol || p_restore.get_type() != Variant::AABB) {
		return;
	}
	const AABB restore = p_restore;
	if (p_cancel) {
		vol->set_size(restore.size);
		vol->set_global_position(restore.position);
		return;
	}
	if (vol->get_size().is_equal_approx(restore.size) &&
			vol->get_global_position().is_equal_approx(restore.position)) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorInterface::get_singleton()->get_editor_undo_redo();
	undo_redo->create_action("Resize Steam Audio Probe Volume", UndoRedo::MERGE_DISABLE, vol);
	undo_redo->add_do_method(vol, "set_size", vol->get_size());
	undo_redo->add_do_method(vol, "set_global_position", vol->get_global_position());
	undo_redo->add_undo_method(vol, "set_global_position", restore.position);
	undo_redo->add_undo_method(vol, "set_size", restore.size);
	undo_redo->commit_action(false);
}

bool SteamAudioProbeVolumeGizmoPlugin::_has_gizmo(Node3D *p_for_node_3d) const {
	return Object::cast_to<SteamAudioProbeVolume>(p_for_node_3d) != nullptr;
}

String SteamAudioProbeVolumeGizmoPlugin::_get_gizmo_name() const {
	return "SteamAudioProbeVolume";
}

int32_t SteamAudioProbeVolumeGizmoPlugin::_get_priority() const {
	return -1;
}

static void add_box_lines(PackedVector3Array &lines, const Vector3 &he) {
	const Vector3 c[8] = {
		Vector3(-he.x, -he.y, -he.z),
		Vector3(he.x, -he.y, -he.z),
		Vector3(he.x, -he.y, he.z),
		Vector3(-he.x, -he.y, he.z),
		Vector3(-he.x, he.y, -he.z),
		Vector3(he.x, he.y, -he.z),
		Vector3(he.x, he.y, he.z),
		Vector3(-he.x, he.y, he.z),
	};
	const int e[] = { 0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7 };
	for (int i = 0; i < 24; i++) {
		lines.push_back(c[e[i]]);
	}
}

static void add_octahedron_lines(PackedVector3Array &lines, const Vector3 &c, float r) {
	const Vector3 px = c + Vector3(r, 0, 0);
	const Vector3 nx = c + Vector3(-r, 0, 0);
	const Vector3 py = c + Vector3(0, r, 0);
	const Vector3 ny = c + Vector3(0, -r, 0);
	const Vector3 pz = c + Vector3(0, 0, r);
	const Vector3 nz = c + Vector3(0, 0, -r);
	const Vector3 v[12][2] = {
		{ py, px }, { py, pz }, { py, nx }, { py, nz },
		{ ny, px }, { ny, pz }, { ny, nx }, { ny, nz },
		{ px, pz }, { pz, nx }, { nx, nz }, { nz, px },
	};
	for (int i = 0; i < 12; i++) {
		lines.push_back(v[i][0]);
		lines.push_back(v[i][1]);
	}
}

void SteamAudioProbeVolumeGizmoPlugin::_redraw(const Ref<EditorNode3DGizmo> &p_gizmo) {
	p_gizmo->clear();
	ensure_materials();

	auto *vol = Object::cast_to<SteamAudioProbeVolume>(p_gizmo->get_node_3d());
	if (vol == nullptr) {
		return;
	}

	const Transform3D global_transform = vol->get_global_transform();
	const Transform3D world_to_local = global_transform.affine_inverse();
	PackedVector3Array box;
	// Match volume_matrix(): generation bounds are axis-aligned in world space.
	add_box_lines(box, global_transform.basis.get_scale() * vol->get_size() * 0.5f);
	for (int i = 0; i < box.size(); i++) {
		box.set(i, world_to_local.xform(global_transform.origin + box[i]));
	}
	Ref<Material> volume_mat = get_material("volume", p_gizmo);
	p_gizmo->add_lines(box, volume_mat);
	p_gizmo->add_collision_segments(box);
	PackedVector3Array handles;
	const Vector3 world_half_extent = global_transform.basis.get_scale().abs() * vol->get_size() * 0.5f;
	for (int axis = 0; axis < 3; axis++) {
		Vector3 offset;
		offset[axis] = world_half_extent[axis];
		handles.push_back(world_to_local.xform(global_transform.origin + offset));
		handles.push_back(world_to_local.xform(global_transform.origin - offset));
	}
	p_gizmo->add_handles(handles, get_material("handles", p_gizmo), PackedInt32Array());

	PackedVector3Array positions = vol->get_probe_positions();
	if (positions.is_empty()) {
		return;
	}
	PackedVector3Array probe_lines;
	const float r = 0.12f;
	for (int i = 0; i < positions.size(); i++) {
		add_octahedron_lines(probe_lines, world_to_local.xform(positions[i]), r);
	}
	Ref<SteamAudioProbeBatchData> data = vol->get_baked_data();
	const bool baked = data.is_valid() && (data->is_pathing_baked() || data->are_reflections_baked());
	Ref<Material> probe_mat = get_material(baked ? "probes_baked" : "probes", p_gizmo);
	p_gizmo->add_lines(probe_lines, probe_mat);
	p_gizmo->add_collision_segments(probe_lines);
}

void SteamAudioEditorPlugin::_enter_tree() {
	inspector.instantiate();
	add_inspector_plugin(inspector);
	gizmos.instantiate();
	add_node_3d_gizmo_plugin(gizmos);
}

void SteamAudioEditorPlugin::_exit_tree() {
	if (inspector.is_valid()) {
		remove_inspector_plugin(inspector);
		inspector.unref();
	}
	if (gizmos.is_valid()) {
		remove_node_3d_gizmo_plugin(gizmos);
		gizmos.unref();
	}
}
