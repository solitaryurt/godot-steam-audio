#ifndef STEAM_AUDIO_PROBE_VOLUME_H
#define STEAM_AUDIO_PROBE_VOLUME_H

#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "phonon.h"
#include "probe_core.hpp"
#include <atomic>

using namespace godot;

class SteamAudioProbeBatchData : public Resource {
	GDCLASS(SteamAudioProbeBatchData, Resource);

	PackedByteArray bytes;
	int probe_count = 0;
	bool pathing_baked = false;

protected:
	static void _bind_methods();

public:
	SteamAudioProbeBatchData() {}
	~SteamAudioProbeBatchData() {}

	PackedByteArray get_bytes() const { return bytes; }
	void set_bytes(PackedByteArray p_bytes) { bytes = p_bytes; }
	int get_probe_count() const { return probe_count; }
	void set_probe_count(int p_count) { probe_count = p_count; }
	bool is_pathing_baked() const { return pathing_baked; }
	void set_pathing_baked(bool p_baked) { pathing_baked = p_baked; }
};

class SteamAudioProbeVolume : public Node3D {
	GDCLASS(SteamAudioProbeVolume, Node3D);

public:
	enum GenerationType {
		GENERATION_CENTROID = 0,
		GENERATION_UNIFORM_FLOOR = 1,
	};

private:
	Vector3 size = Vector3(10.f, 4.f, 10.f);
	GenerationType generation = GENERATION_UNIFORM_FLOOR;
	float spacing = 2.0f;
	float height = 1.5f;
	float vis_range = 50.0f;
	float path_range = 100.0f;
	int bake_samples = 1;
	float bake_radius = 1.0f;
	float bake_threshold = 0.5f;
	int bake_threads = 4;
	Ref<SteamAudioProbeBatchData> baked_data;
	IPLProbeBatch runtime_batch = nullptr;
	std::atomic<bool> registered{ false };

	IPLMatrix4x4 volume_matrix() const;
	void ready_internal();
	void register_batch();
	void unregister_batch();

protected:
	static void _bind_methods();

public:
	SteamAudioProbeVolume();
	~SteamAudioProbeVolume();
	void _notification(int p_what);

	void generate_probes();
	void bake_pathing();
	int get_probe_count() const;

	Vector3 get_size() const { return size; }
	void set_size(Vector3 p_size);
	GenerationType get_generation() const { return generation; }
	void set_generation(GenerationType p_generation) { generation = p_generation; }
	float get_spacing() const { return spacing; }
	void set_spacing(float p_spacing) { spacing = p_spacing; }
	float get_height() const { return height; }
	void set_height(float p_height) { height = p_height; }
	float get_vis_range() const { return vis_range; }
	void set_vis_range(float p_vis_range) { vis_range = p_vis_range; }
	float get_path_range() const { return path_range; }
	void set_path_range(float p_path_range) { path_range = p_path_range; }
	int get_bake_samples() const { return bake_samples; }
	void set_bake_samples(int p_samples) { bake_samples = p_samples; }
	float get_bake_radius() const { return bake_radius; }
	void set_bake_radius(float p_radius) { bake_radius = p_radius; }
	float get_bake_threshold() const { return bake_threshold; }
	void set_bake_threshold(float p_threshold) { bake_threshold = p_threshold; }
	int get_bake_threads() const { return bake_threads; }
	void set_bake_threads(int p_threads) { bake_threads = p_threads; }
	Ref<SteamAudioProbeBatchData> get_baked_data() const { return baked_data; }
	void set_baked_data(Ref<SteamAudioProbeBatchData> p_data) { baked_data = p_data; }

	PackedStringArray _get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(SteamAudioProbeVolume::GenerationType);

#endif // STEAM_AUDIO_PROBE_VOLUME_H
