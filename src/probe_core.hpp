#ifndef STEAM_AUDIO_PROBE_CORE_H
#define STEAM_AUDIO_PROBE_CORE_H

#include <phonon.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Godot-free helpers around IPL probe batches. Used by SteamAudioProbeVolume
// and by tests/probe_batch_test.cpp.

IPLContext probe_core_create_context(std::string *err = nullptr);
void probe_core_destroy_context(IPLContext *ctx);

IPLScene probe_core_create_empty_scene(IPLContext ctx, std::string *err = nullptr);
// Axis-aligned solid box centered at origin, for UNIFORMFLOOR generation / bake tests.
IPLScene probe_core_create_box_scene(IPLContext ctx, float hx, float hy, float hz, std::string *err = nullptr);
bool probe_core_add_box(IPLScene scene, float cx, float cy, float cz, float hx, float hy, float hz, std::string *err = nullptr);
void probe_core_destroy_scene(IPLScene *scene);

// volume maps the unit cube [0,1]^3 into world space (row-major, p' = M p).
bool probe_core_generate_batch(IPLContext ctx, IPLScene scene, IPLProbeGenerationType type,
		IPLMatrix4x4 volume, float spacing, float height,
		IPLProbeBatch *out_batch, int *out_count, std::string *err = nullptr);

bool probe_core_save_batch(IPLContext ctx, IPLProbeBatch batch, std::vector<uint8_t> *out, std::string *err = nullptr);
bool probe_core_load_batch(IPLContext ctx, const uint8_t *data, size_t size,
		IPLProbeBatch *out_batch, int *out_count, std::string *err = nullptr);

bool probe_core_bake_pathing(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_samples, float radius, float threshold, float vis_range, float path_range,
		int num_threads, std::string *err = nullptr);

int probe_core_batch_num_probes(IPLProbeBatch batch);
IPLsize probe_core_pathing_data_size(IPLProbeBatch batch);

void probe_core_release_batch(IPLProbeBatch *batch);

// Unit-cube -> centered box of `size` at `origin`, axis-aligned.
IPLMatrix4x4 probe_core_volume_matrix(float ox, float oy, float oz, float sx, float sy, float sz);

#endif // STEAM_AUDIO_PROBE_CORE_H
