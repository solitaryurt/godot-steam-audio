extends SteamAudioProbeVolume

func _ready() -> void:
	if baked_data != null and baked_data.pathing_baked:
		prints("[pathing demo] using saved bake probes=", get_probe_count(), "path=", save_path)
		return
	if not save_path.is_empty() and ResourceLoader.exists(save_path):
		baked_data = load(save_path)
		if baked_data != null and baked_data.pathing_baked:
			prints("[pathing demo] loaded bake from", save_path, "probes=", get_probe_count())
			return
	prints("[pathing demo] no saved bake; baking pathing...")
	bake_pathing()
	prints("[pathing demo] done probes=", get_probe_count(), "baked=", baked_data != null and baked_data.pathing_baked)
