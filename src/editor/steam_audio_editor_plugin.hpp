#ifndef STEAM_AUDIO_EDITOR_PLUGIN_H
#define STEAM_AUDIO_EDITOR_PLUGIN_H

#include "godot_cpp/classes/button.hpp"
#include "godot_cpp/classes/camera3d.hpp"
#include "godot_cpp/classes/editor_inspector_plugin.hpp"
#include "godot_cpp/classes/editor_node3d_gizmo_plugin.hpp"
#include "godot_cpp/classes/editor_plugin.hpp"
#include "godot_cpp/classes/label.hpp"
#include "godot_cpp/classes/progress_bar.hpp"
#include "godot_cpp/classes/v_box_container.hpp"
#include "probe_volume.hpp"

using namespace godot;

class SteamAudioProbeBakePanel : public VBoxContainer {
	GDCLASS(SteamAudioProbeBakePanel, VBoxContainer);

	ObjectID volume_id;
	Button *generate_btn = nullptr;
	Button *pathing_btn = nullptr;
	Button *reflections_btn = nullptr;
	Button *cancel_btn = nullptr;
	ProgressBar *progress = nullptr;
	Label *status = nullptr;

	SteamAudioProbeVolume *volume() const;
	void refresh();
	void _on_generate();
	void _on_bake_pathing();
	void _on_bake_reflections();
	void _on_cancel();

protected:
	static void _bind_methods();

public:
	void setup(SteamAudioProbeVolume *p_volume);
	void _process(double p_delta) override;
};

class SteamAudioProbeVolumeInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(SteamAudioProbeVolumeInspectorPlugin, EditorInspectorPlugin);

protected:
	static void _bind_methods() {}

public:
	bool _can_handle(Object *p_object) const override;
	void _parse_begin(Object *p_object) override;
};

class SteamAudioProbeVolumeGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(SteamAudioProbeVolumeGizmoPlugin, EditorNode3DGizmoPlugin);

	bool materials_ready = false;
	Vector3 handle_start_position;
	Vector3 handle_start_size;
	Vector3 handle_start_scale;
	void ensure_materials();

protected:
	static void _bind_methods() {}

public:
	bool _has_gizmo(Node3D *p_for_node_3d) const override;
	String _get_gizmo_name() const override;
	int32_t _get_priority() const override;
	String _get_handle_name(const Ref<EditorNode3DGizmo> &p_gizmo, int32_t p_handle_id, bool p_secondary) const override;
	Variant _get_handle_value(const Ref<EditorNode3DGizmo> &p_gizmo, int32_t p_handle_id, bool p_secondary) const override;
	void _begin_handle_action(const Ref<EditorNode3DGizmo> &p_gizmo, int32_t p_handle_id, bool p_secondary) override;
	void _set_handle(const Ref<EditorNode3DGizmo> &p_gizmo, int32_t p_handle_id, bool p_secondary,
			Camera3D *p_camera, const Vector2 &p_screen_pos) override;
	void _commit_handle(const Ref<EditorNode3DGizmo> &p_gizmo, int32_t p_handle_id, bool p_secondary,
			const Variant &p_restore, bool p_cancel) override;
	void _redraw(const Ref<EditorNode3DGizmo> &p_gizmo) override;
};

class SteamAudioEditorPlugin : public EditorPlugin {
	GDCLASS(SteamAudioEditorPlugin, EditorPlugin);

	Ref<SteamAudioProbeVolumeInspectorPlugin> inspector;
	Ref<SteamAudioProbeVolumeGizmoPlugin> gizmos;

protected:
	static void _bind_methods() {}

public:
	void _enter_tree() override;
	void _exit_tree() override;
	String _get_plugin_name() const override { return "Steam Audio"; }
};

#endif // STEAM_AUDIO_EDITOR_PLUGIN_H
