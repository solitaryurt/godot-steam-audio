#ifndef STEAM_AUDIO_PROBE_VOLUME_H
#define STEAM_AUDIO_PROBE_VOLUME_H

#include "godot_cpp/classes/node3d.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "phonon.h"
#include "probe_core.hpp"
#include <atomic>
#include <thread>
#include <vector>

using namespace godot;

class SteamAudioProbeBatchData : public Resource {
	GDCLASS(SteamAudioProbeBatchData, Resource);

	PackedByteArray bytes;
	PackedVector3Array probe_positions;
	PackedFloat32Array probe_radii;
	int probe_count = 0;
	bool pathing_baked = false;
	bool reflections_baked = false;

protected:
	static void _bind_methods();

public:
	SteamAudioProbeBatchData() {}
	~SteamAudioProbeBatchData() {}

	PackedByteArray get_bytes() const { return bytes; }
	void set_bytes(PackedByteArray p_bytes) { bytes = p_bytes; }
	PackedVector3Array get_probe_positions() const { return probe_positions; }
	void set_probe_positions(PackedVector3Array p_positions) { probe_positions = p_positions; }
	PackedFloat32Array get_probe_radii() const { return probe_radii; }
	void set_probe_radii(PackedFloat32Array p_radii) { probe_radii = p_radii; }
	int get_probe_count() const { return probe_count; }
	void set_probe_count(int p_count) { probe_count = p_count; }
	bool is_pathing_baked() const { return pathing_baked; }
	void set_pathing_baked(bool p_baked) { pathing_baked = p_baked; }
	bool are_reflections_baked() const { return reflections_baked; }
	void set_reflections_baked(bool p_baked) { reflections_baked = p_baked; }
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
	int reflection_rays = 1024;
	int reflection_diffuse_samples = 32;
	int reflection_bounces = 8;
	float reflection_simulated_duration = 1.0f;
	float reflection_saved_duration = 1.0f;
	int reflection_order = 1;
	int reflection_threads = 4;
	float reflection_irradiance_min_distance = 1.0f;
	String save_path;
	Ref<SteamAudioProbeBatchData> baked_data;
	IPLProbeBatch runtime_batch = nullptr;
	std::atomic<bool> registered{ false };

	enum BakeState {
		BAKE_IDLE = 0,
		BAKE_RUNNING = 1,
		BAKE_DONE_OK = 2,
		BAKE_DONE_FAIL = 3,
		BAKE_DONE_CANCEL = 4,
		BAKE_FINALIZING = 5,
	};
	enum BakeKind {
		BAKE_PATHING,
		BAKE_REFLECTIONS,
	};
	struct BakeSettings {
		int path_samples;
		float path_radius;
		float path_threshold;
		float path_vis_range;
		float path_range;
		int path_threads;
		int reflection_rays;
		int reflection_diffuse_samples;
		int reflection_bounces;
		float reflection_simulated_duration;
		float reflection_saved_duration;
		int reflection_order;
		int reflection_threads;
		float reflection_irradiance_min_distance;
	};
	std::atomic<int> bake_state{ BAKE_IDLE };
	std::atomic<float> bake_progress{ 0.f };
	std::atomic<bool> bake_cancel_requested{ false };
	uint64_t bake_sequence = 0;
	std::thread bake_thread;
	Ref<SteamAudioProbeBatchData> bake_data;
	IPLContext bake_ctx = nullptr;
	IPLScene bake_scene = nullptr;
	IPLProbeBatch bake_batch = nullptr;
	std::vector<uint8_t> bake_bytes;
	int bake_count = 0;
	std::string bake_error;
	BakeKind bake_kind = BAKE_PATHING;
	BakeSettings bake_settings{};
	bool owns_global_bake = false;
	static std::atomic<bool> global_bake_busy;

	IPLMatrix4x4 volume_matrix() const;
	void ready_internal();
	void register_batch();
	void unregister_batch();
	void apply_serialized_batch(const std::vector<uint8_t> &bytes, int count, bool pathing,
			const std::vector<IPLVector3> *positions, const std::vector<float> *radii = nullptr);
	String resolve_save_path();
	void start_bake(BakeKind p_kind);
	void bake_worker();
	void finish_bake();
	void join_bake_thread();
	void release_bake_job();
	void refresh_editor_visuals();
	static void IPLCALL bake_progress_cb(IPLfloat32 progress, void *user);

protected:
	static void _bind_methods();

public:
	SteamAudioProbeVolume();
	~SteamAudioProbeVolume();
	void _notification(int p_what);

	void generate_probes();
	void bake_pathing();
	void bake_reflections();
	void cancel_bake();
	Error save_baked_data();
	int get_probe_count() const;
	bool is_baking() const { return bake_state.load() != BAKE_IDLE; }
	bool is_bake_cancelling() const { return is_baking() && bake_cancel_requested.load(); }
	bool is_baking_reflections() const { return is_baking() && bake_kind == BAKE_REFLECTIONS; }
	float get_bake_progress() const { return bake_progress.load(); }
	PackedVector3Array get_probe_positions() const;

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
	int get_reflection_rays() const { return reflection_rays; }
	void set_reflection_rays(int p_value) { reflection_rays = p_value; }
	int get_reflection_diffuse_samples() const { return reflection_diffuse_samples; }
	void set_reflection_diffuse_samples(int p_value) { reflection_diffuse_samples = p_value; }
	int get_reflection_bounces() const { return reflection_bounces; }
	void set_reflection_bounces(int p_value) { reflection_bounces = p_value; }
	float get_reflection_simulated_duration() const { return reflection_simulated_duration; }
	void set_reflection_simulated_duration(float p_value) { reflection_simulated_duration = p_value; }
	float get_reflection_saved_duration() const { return reflection_saved_duration; }
	void set_reflection_saved_duration(float p_value) { reflection_saved_duration = p_value; }
	int get_reflection_order() const { return reflection_order; }
	void set_reflection_order(int p_value) { reflection_order = p_value; }
	int get_reflection_threads() const { return reflection_threads; }
	void set_reflection_threads(int p_value) { reflection_threads = p_value; }
	float get_reflection_irradiance_min_distance() const { return reflection_irradiance_min_distance; }
	void set_reflection_irradiance_min_distance(float p_value) { reflection_irradiance_min_distance = p_value; }
	String get_save_path() const { return save_path; }
	void set_save_path(String p_path) { save_path = p_path; }
	Ref<SteamAudioProbeBatchData> get_baked_data() const { return baked_data; }
	void set_baked_data(Ref<SteamAudioProbeBatchData> p_data);

	PackedStringArray _get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(SteamAudioProbeVolume::GenerationType);

#endif // STEAM_AUDIO_PROBE_VOLUME_H
