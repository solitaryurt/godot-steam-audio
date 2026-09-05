#ifndef STEAM_AUDIO_PROBE_CORE_H
#define STEAM_AUDIO_PROBE_CORE_H

#include <phonon.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Godot-free helpers around IPL probe batches. Used by SteamAudioProbeVolume
// and by tests/probe_batch_test.cpp.

struct ProbeCoreNeighborhood {
	bool has_visible_probe;
	bool has_guaranteed_visible_probe;
};

template <typename VisibilityTest>
ProbeCoreNeighborhood probe_core_query_neighborhood(const std::vector<IPLSphere> &probes,
		IPLVector3 point, VisibilityTest is_visible) {
	int visible = 0;
	int occluded = 0;
	for (const auto &probe : probes) {
		float dx = probe.center.x - point.x;
		float dy = probe.center.y - point.y;
		float dz = probe.center.z - point.z;
		// SDK 4.5.3 includes the boundary and uses inverse-distance weights.
		if (dx * dx + dy * dy + dz * dz <= probe.radius * probe.radius) {
			if (is_visible(point, probe.center)) {
				++visible;
			} else {
				++occluded;
			}
		}
	}
	// The SDK selects up to eight BVH neighbors before testing occlusion.
	// Fewer than eight occluded influences cannot fill a hidden-only selection.
	return { visible > 0, visible > 0 && occluded < 8 };
}

IPLContext probe_core_create_context(std::string *err = nullptr);
void probe_core_destroy_context(IPLContext *ctx);

IPLScene probe_core_create_empty_scene(IPLContext ctx, std::string *err = nullptr);
// Axis-aligned solid box centered at origin, for UNIFORMFLOOR generation / bake tests.
IPLScene probe_core_create_box_scene(IPLContext ctx, float hx, float hy, float hz, std::string *err = nullptr);
bool probe_core_add_box(IPLScene scene, float cx, float cy, float cz, float hx, float hy, float hz, std::string *err = nullptr);
void probe_core_destroy_scene(IPLScene *scene);

// volume maps [-0.5,0.5]^3 into world space, using SDK 4.5.3's probe matrix
// convention (column-major storage). Prefer probe_core_volume_matrix.
bool probe_core_generate_batch(IPLContext ctx, IPLScene scene, IPLProbeGenerationType type,
		IPLMatrix4x4 volume, float spacing, float height,
		IPLProbeBatch *out_batch, int *out_count, std::string *err = nullptr,
		std::vector<IPLVector3> *out_positions = nullptr, std::vector<float> *out_radii = nullptr);

bool probe_core_save_batch(IPLContext ctx, IPLProbeBatch batch, std::vector<uint8_t> *out, std::string *err = nullptr);
bool probe_core_load_batch(IPLContext ctx, const uint8_t *data, size_t size,
		IPLProbeBatch *out_batch, int *out_count, std::string *err = nullptr);

// Bakers replace their target layer in a job-local batch, preserving other layers.
// Callers must serialize all bake jobs, including cancellation delivery: SDK
// baker state is process-global, not per context. Success requires new layer data.
// Cancel via the matching core API during the bake (e.g. its progress callback),
// not before SDK entry. Cancellation returns false; release the job-local batch
// without querying, saving, removing data from, or rebaking it.
// Callers must also discard jobs with a late cancellation request after return.
// Pathing uses finish-and-discard: SDK 4.5.3 native cancellation is unsafe. The
// async editor stays busy until the worker finishes; exit/join can wait for the
// full bake. Reflections use native cancellation and can leave partial layers.
bool probe_core_bake_pathing(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_samples, float radius, float threshold, float vis_range, float path_range,
		int num_threads, std::string *err = nullptr,
		IPLProgressCallback progress_cb = nullptr, void *progress_user = nullptr);
void probe_core_cancel_pathing_bake(IPLContext ctx);

bool probe_core_bake_reflections(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_rays, int num_diffuse_samples, int num_bounces,
		float simulated_duration, float saved_duration, int order, int num_threads,
		float irradiance_min_distance, int bake_batch_size, std::string *err = nullptr,
		IPLProgressCallback progress_cb = nullptr, void *progress_user = nullptr);
void probe_core_cancel_reflections_bake(IPLContext ctx);

int probe_core_batch_num_probes(IPLProbeBatch batch);
IPLsize probe_core_pathing_data_size(IPLProbeBatch batch);
IPLsize probe_core_reflections_data_size(IPLProbeBatch batch);

void probe_core_release_batch(IPLProbeBatch *batch);

// SDK probe-local cube [-0.5,0.5]^3 -> box of full `size` at `origin`, axis-aligned.
IPLMatrix4x4 probe_core_volume_matrix(float ox, float oy, float oz, float sx, float sy, float sz);

#endif // STEAM_AUDIO_PROBE_CORE_H
