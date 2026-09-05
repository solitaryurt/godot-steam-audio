@tool
extends SceneTree
## Run against a freshly built editor extension, from the repository root:
## godot --headless --editor --path project --script ../tests/probe_volume_test.gd
## Uses only a unique user:// directory; never saves the edited project scene.
## Expected logs include busy refusals and one missing-metadata bake error.
## Headless checks cannot verify rendered gizmos. In the editor, also move a
## generated volume and its parent: probe markers must remain at world positions.

var failures := 0
var work_dir := "user://probe_volume_test_%s_%s" % [OS.get_process_id(), Time.get_ticks_usec()]
var scenes: Array[Node3D] = []


func _initialize() -> void:
	call_deferred("_run")


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func scene(file_name: String) -> Node3D:
	var node := Node3D.new()
	node.scene_file_path = work_dir.path_join(file_name)
	root.add_child(node)
	scenes.append(node)
	return node


func volume(parent: Node3D, node_name: String):
	var node = ClassDB.instantiate("SteamAudioProbeVolume")
	node.name = node_name
	parent.add_child(node)
	node.owner = parent
	var data = ClassDB.instantiate("SteamAudioProbeBatchData")
	# Saving needs nonempty bytes, but these naming fixtures never enter the SDK.
	data.bytes = PackedByteArray([1])
	node.baked_data = data
	return node


func test_names() -> void:
	var first = volume(scene("a_b.tscn"), "C")
	var second = volume(scene("a.tscn"), "b_C")
	check(first.save_baked_data() == OK, "first automatic save failed")
	check(second.save_baked_data() == OK, "second automatic save failed")
	check(first.save_path != second.save_path, "scene/node underscore collision")
	check(first.save_path.get_file() == "8_a_b.tscn_C.res", "unexpected length-prefixed filename")
	var original_path: String = first.save_path
	first.name = "Renamed"
	first.baked_data.bytes = PackedByteArray([9])
	check(first.save_baked_data() == OK, "resave failed")
	check(first.save_path == original_path, "explicit saved binding changed after rename")
	first.save_path = ""
	check(first.save_baked_data() == OK, "resource-backed resave failed")
	check(first.save_path == original_path, "standalone resource binding changed")
	var reloaded = ResourceLoader.load(original_path, "", ResourceLoader.CACHE_MODE_IGNORE)
	check(reloaded != null and reloaded.bytes == PackedByteArray([9]), "bound save did not update data")

	var occupied := work_dir.path_join("8_map.tscn_Probe.res")
	var file := FileAccess.open(occupied, FileAccess.WRITE)
	check(file != null, "could not create unrelated asset fixture")
	if file == null:
		return
	file.store_string("unrelated existing asset")
	file.close()
	var occupied_dir := work_dir.path_join("8_map.tscn_Probe_1.res")
	check(DirAccess.make_dir_absolute(occupied_dir) == OK, "could not create directory collision")
	var third = volume(scene("map.tscn"), "Probe")
	check(third.save_baked_data() == OK, "occupied automatic save failed")
	check(third.save_path == work_dir.path_join("8_map.tscn_Probe_2.res"), "did not skip occupied file/directory")
	check(FileAccess.get_file_as_string(occupied) == "unrelated existing asset", "overwrote unrelated asset")
	var fourth = volume(scene("map.tscn"), "Probe")
	check(fourth.save_baked_data() == OK, "duplicate scene identity save failed")
	check(fourth.save_path != third.save_path, "duplicate scene identity overwrote existing bake")
	var explicit = volume(scene("explicit.tscn"), "Probe")
	explicit.save_path = work_dir.path_join("legacy_scene_Probe.tres")
	check(explicit.save_baked_data() == OK, "explicit legacy-style path failed")
	check(explicit.save_path.ends_with("/legacy_scene_Probe.tres"), "explicit path was rewritten")


func test_busy() -> void:
	var parent := scene("busy.tscn")
	var first = volume(parent, "First")
	first.baked_data = null
	first.generation = 0 # One centroid keeps real SDK bakes small.
	first.bake_threads = 1
	first.generate_probes()
	check(first.get_probe_count() == 1, "centroid generation failed")
	if first.get_probe_count() != 1:
		return
	check(first.is_transform_notification_enabled(), "editor global transform notifications disabled")
	var positions: PackedVector3Array = first.get_probe_positions()
	first.position += Vector3.ONE
	parent.position += Vector3.ONE
	first.force_update_transform()
	check(first.get_probe_positions() == positions, "movement changed world-space probe metadata")
	var second = volume(parent, "Second")
	second.baked_data = first.baked_data.duplicate()
	var third = volume(parent, "Third")
	third.baked_data = first.baked_data.duplicate()
	third.bake_threads = 1
	third.reflection_threads = 1
	third.reflection_rays = 32
	third.reflection_diffuse_samples = 1
	third.reflection_bounces = 1
	third.reflection_simulated_duration = 0.1
	third.reflection_saved_duration = 0.1
	third.reflection_order = 0

	# No frame yield: even an already-finished worker still owns the unfinalized job.
	first.bake_pathing()
	check(first.is_baking(), "editor bake did not start asynchronously")
	second.bake_pathing()
	check(not second.is_baking(), "second path bake acquired busy ownership")
	second.bake_reflections()
	check(not second.is_baking(), "second reflection bake acquired busy ownership")
	second.free() # Destruction of a rejected job must not clear another owner's flag.
	third.bake_reflections()
	check(not third.is_baking(), "non-owner destruction cleared busy ownership")
	first.cancel_bake()
	third.bake_pathing()
	check(not third.is_baking(), "cancel request released ownership before SDK cleanup")
	parent.remove_child(first) # Joins the worker and releases its SDK job.
	check(not first.is_baking(), "exit-tree did not finalize cancellation")
	first.free()

	var valid_radii: PackedFloat32Array = third.baked_data.probe_radii
	third.baked_data.probe_radii = PackedFloat32Array()
	third.bake_pathing()
	check(not third.is_baking(), "invalid metadata started a bake")
	third.baked_data.probe_radii = valid_radii
	third.bake_reflections()
	check(third.is_baking(), "setup failure or exit-tree leaked global ownership")
	third.free() # Active-owner destruction must cancel/join before releasing ownership.

	var last = volume(parent, "Last")
	last.baked_data = null
	last.generation = 0
	last.bake_threads = 1
	last.generate_probes()
	var results: Array[bool] = []
	last.bake_finished.connect(func(success: bool): results.append(success))
	# Normal completion must also release ownership for a second asynchronous job.
	for attempt in range(2):
		last.bake_pathing()
		check(last.is_baking(), "global ownership leaked before completion test")
		var deadline := Time.get_ticks_msec() + 10000
		while last.is_baking() and Time.get_ticks_msec() < deadline:
			await process_frame
		check(not last.is_baking(), "bake did not finalize within 10 seconds")
		if last.is_baking():
			break
	check(results == [true, true], "normal bake completion/rebake failed")


func _run() -> void:
	if not Engine.is_editor_hint() or not ClassDB.class_exists("SteamAudioProbeVolume"):
		push_error("Requires --editor and a freshly built Steam Audio GDExtension")
		quit(1)
		return
	if DirAccess.make_dir_recursive_absolute(work_dir) != OK:
		push_error("Could not create test directory: " + work_dir)
		quit(1)
		return
	test_names()
	await test_busy()
	for node in scenes:
		node.free()
	for file_name in DirAccess.get_files_at(work_dir):
		check(DirAccess.remove_absolute(work_dir.path_join(file_name)) == OK, "fixture file cleanup failed")
	for dir_name in DirAccess.get_directories_at(work_dir):
		check(DirAccess.remove_absolute(work_dir.path_join(dir_name)) == OK, "fixture directory cleanup failed")
	check(DirAccess.remove_absolute(work_dir) == OK, "test directory cleanup failed")
	print("probe_volume_test: %d failures" % failures)
	quit(0 if failures == 0 else 1)
