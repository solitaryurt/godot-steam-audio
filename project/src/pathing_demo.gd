extends SteamAudioProbeVolume

# First play bakes pathing for this volume if the scene has no saved batch yet.
func _ready() -> void:
	if baked_data != null and baked_data.pathing_baked:
		prints("[pathing demo] using saved bake probes=", get_probe_count())
		return
	prints("[pathing demo] baking pathing for the demo volume...")
	bake_pathing()
	prints("[pathing demo] done probes=", get_probe_count(), "baked=", baked_data != null and baked_data.pathing_baked)
