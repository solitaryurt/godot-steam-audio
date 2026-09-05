#include "probe_core.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_fails = 0;

static void expect(bool cond, const char *msg) {
	if (!cond) {
		std::fprintf(stderr, "FAIL: %s\n", msg);
		g_fails++;
	} else {
		std::printf("ok: %s\n", msg);
	}
}

int main() {
	std::string err;

	// Exercise the exact eligibility query used by the runtime server without Godot.
	const IPLVector3 point{ 0.f, 0.f, 0.f };
	auto visible_on_right = [](IPLVector3, IPLVector3 probe) { return probe.x >= 0.f; };
	std::vector<IPLSphere> probes{ { { 2.f, 0.f, 0.f }, 2.f } };
	auto neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(neighborhood.has_visible_probe && neighborhood.has_guaranteed_visible_probe,
			"visible boundary probe establishes runtime eligibility");
	probes[0].radius = 1.99f;
	neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(!neighborhood.has_visible_probe && !neighborhood.has_guaranteed_visible_probe,
			"outside probe does not establish runtime eligibility");
	probes.assign(9, { { 1.f, 0.f, 0.f }, 2.f });
	neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(neighborhood.has_guaranteed_visible_probe, "nine visible influences remain eligible");
	for (int i = 0; i < 7; ++i) {
		probes[size_t(i)].center.x = -1.f;
	}
	neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(neighborhood.has_guaranteed_visible_probe,
			"seven hidden influences cannot fill the SDK's eight neighbors");
	probes[7].center.x = -1.f;
	neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(neighborhood.has_visible_probe && !neighborhood.has_guaranteed_visible_probe,
			"eight hidden influences keep ambiguous neighborhoods ineligible");
	probes[8].center.x = -1.f;
	neighborhood = probe_core_query_neighborhood(probes, point, visible_on_right);
	expect(!neighborhood.has_visible_probe && !neighborhood.has_guaranteed_visible_probe,
			"fully occluded batches cannot contribute or dilute reflection weights");
	neighborhood = probe_core_query_neighborhood({}, point, visible_on_right);
	expect(!neighborhood.has_visible_probe && !neighborhood.has_guaranteed_visible_probe,
			"empty neighborhoods do not establish eligibility");

	IPLContext ctx = probe_core_create_context(&err);
	expect(ctx != nullptr, "create IPL context");
	if (ctx == nullptr) {
		std::fprintf(stderr, "  %s\n", err.c_str());
		return 1;
	}

	IPLScene empty = probe_core_create_empty_scene(ctx, &err);
	expect(empty != nullptr, "create empty scene");

	IPLMatrix4x4 volume = probe_core_volume_matrix(0.f, 2.f, 0.f, 10.f, 4.f, 10.f);

	// Step 1: centroid generate + save/load (no bake).
	IPLProbeBatch centroid = nullptr;
	int centroid_n = 0;
	std::vector<IPLVector3> centroid_pos;
	std::vector<float> centroid_radii;
	bool gen_ok = probe_core_generate_batch(ctx, empty, IPL_PROBEGENERATIONTYPE_CENTROID,
			volume, 2.f, 1.5f, &centroid, &centroid_n, &err, &centroid_pos, &centroid_radii);
	expect(gen_ok, "generate centroid batch");
	expect(centroid_n == 1, "centroid batch has 1 probe");
	expect(int(centroid_pos.size()) == centroid_n, "centroid positions match count");
	expect(!centroid_radii.empty() && int(centroid_radii.size()) == centroid_n && centroid_radii[0] > 0.f,
			"centroid influence radii match count");
	if (!centroid_pos.empty()) {
		std::printf("  centroid probe = (%.3f, %.3f, %.3f)\n",
				centroid_pos[0].x, centroid_pos[0].y, centroid_pos[0].z);
		expect(centroid_pos[0].x == 0.f && centroid_pos[0].y == 2.f && centroid_pos[0].z == 0.f,
				"centroid is exactly (0,2,0)");
		expect(centroid_radii.size() == 1 && centroid_radii[0] == 2.f, "centroid radius is half the smallest dimension");
	}
	if (!gen_ok) {
		std::fprintf(stderr, "  %s\n", err.c_str());
	}

	std::vector<uint8_t> bytes;
	bool save_ok = gen_ok && probe_core_save_batch(ctx, centroid, &bytes, &err);
	expect(save_ok && !bytes.empty(), "serialize centroid batch");
	if (!save_ok) {
		std::fprintf(stderr, "  %s\n", err.c_str());
	}

	IPLProbeBatch loaded = nullptr;
	int loaded_n = 0;
	bool load_ok = save_ok && probe_core_load_batch(ctx, bytes.data(), bytes.size(), &loaded, &loaded_n, &err);
	expect(load_ok, "deserialize centroid batch");
	expect(loaded_n == centroid_n, "loaded probe count matches");
	if (!load_ok) {
		std::fprintf(stderr, "  %s\n", err.c_str());
	}

	probe_core_release_batch(&centroid);
	probe_core_release_batch(&loaded);

	// Exercise all translation components and unequal dimensions independently.
	volume = probe_core_volume_matrix(3.f, 7.f, -5.f, 6.f, 10.f, 14.f);
	expect(probe_core_generate_batch(ctx, empty, IPL_PROBEGENERATIONTYPE_CENTROID,
			volume, 2.f, 1.5f, &centroid, &centroid_n, &err, &centroid_pos, &centroid_radii),
			"generate translated asymmetric centroid");
	expect(centroid_pos.size() == 1 && centroid_pos[0].x == 3.f &&
			centroid_pos[0].y == 7.f && centroid_pos[0].z == -5.f,
			"translated centroid is exactly (3,7,-5)");
	expect(centroid_radii.size() == 1 && centroid_radii[0] == 3.f,
			"asymmetric centroid radius is exactly 3");
	probe_core_release_batch(&centroid);

	// Uniform floor over a solid box.
	IPLScene box = probe_core_create_box_scene(ctx, 8.f, 0.25f, 8.f, &err);
	expect(box != nullptr, "create box scene");
	IPLProbeBatch floor = nullptr;
	int floor_n = 0;
	std::vector<IPLVector3> floor_pos;
	std::vector<float> floor_radii;
	IPLMatrix4x4 floor_vol = probe_core_volume_matrix(0.f, 2.f, 0.f, 16.f, 4.f, 16.f);
	bool floor_ok = box && probe_core_generate_batch(ctx, box, IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
			floor_vol, 4.f, 1.5f, &floor, &floor_n, &err, &floor_pos, &floor_radii);
	expect(floor_ok, "generate uniform-floor batch");
	expect(floor_n == 25, "uniform-floor produced exactly 25 probes");
	expect(int(floor_pos.size()) == floor_n, "uniform-floor positions match count");
	expect(!floor_radii.empty() && int(floor_radii.size()) == floor_n && floor_radii[0] > 0.f,
			"uniform-floor influence radii match count");
	if (!floor_ok) {
		std::fprintf(stderr, "  %s\n", err.c_str());
	} else {
		std::printf("  uniform-floor probe count = %d\n", floor_n);
	}
	bool exact_floor = floor_pos.size() == 25 && floor_radii.size() == 25;
	for (int x = 0; x < 5 && exact_floor; ++x) {
		for (int z = 0; z < 5; ++z) {
			const auto &p = floor_pos[size_t(x * 5 + z)];
			exact_floor = exact_floor && p.x == -8.f + 4.f * x &&
					p.y == 1.75f && p.z == -8.f + 4.f * z && floor_radii[size_t(x * 5 + z)] == 4.f;
		}
	}
	expect(exact_floor, "floor grid has exact coordinates, floor-relative height, and radii");

	IPLScene translated_scene = probe_core_create_empty_scene(ctx, &err);
	IPLProbeBatch translated_batch = nullptr;
	std::vector<IPLVector3> translated_pos;
	std::vector<float> translated_radii;
	bool translated_ok = probe_core_add_box(translated_scene, 11.f, 5.f, -9.f, 7.f, 0.25f, 5.f, &err) &&
			probe_core_generate_batch(ctx, translated_scene, IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
					probe_core_volume_matrix(11.f, 7.f, -9.f, 12.f, 4.f, 8.f), 4.f, 1.5f,
					&translated_batch, nullptr, &err, &translated_pos, &translated_radii);
	expect(translated_ok, "generate translated asymmetric floor volume");
	bool exact_translated = translated_pos.size() == 12 && translated_radii.size() == 12;
	for (int x = 0; x < 4 && exact_translated; ++x) {
		for (int z = 0; z < 3; ++z) {
			const auto &p = translated_pos[size_t(x * 3 + z)];
			exact_translated = exact_translated && p.x == 5.f + 4.f * x &&
					p.y == 6.75f && p.z == -13.f + 4.f * z && translated_radii[size_t(x * 3 + z)] == 4.f;
		}
	}
	expect(exact_translated, "translated floor grid has exact world coordinates and radii");
	probe_core_release_batch(&translated_batch);
	probe_core_destroy_scene(&translated_scene);

	// Block the direct source-listener ray. Otherwise pathing can produce nonzero
	// SH even with no baked layer, which makes a positive-only test meaningless.
	expect(probe_core_add_box(box, 0.f, 2.f, 0.f, 0.25f, 2.f, 2.f, &err),
			"add wall requiring an indirect baked path");

	// Step 2: bake PATHING / DYNAMIC into the floor batch.
	bool bake_ok = floor_ok && probe_core_bake_pathing(ctx, box, floor, 1, 1.f, 0.5f, 20.f, 40.f, 2, &err);
	expect(bake_ok, "bake pathing");
	IPLsize path_bytes = floor_ok ? probe_core_pathing_data_size(floor) : 0;
	expect(path_bytes > 0, "baked pathing layer is non-empty");
	std::printf("  pathing data size = %zu\n", (size_t)path_bytes);

	// Step 3: bake listener-centric convolution reverb into the same batch.
	bool reflections_ok = floor_ok && probe_core_bake_reflections(ctx, box, floor,
			256, 32, 4, 0.1f, 0.1f, 1, 1, 1.f, 1, &err);
	expect(reflections_ok, "bake reflections");
	IPLsize reflection_bytes = floor_ok ? probe_core_reflections_data_size(floor) : 0;
	expect(reflection_bytes > 0, "baked reflections layer is non-empty");
	expect(floor_ok && probe_core_pathing_data_size(floor) == path_bytes,
			"successful reflection bake preserves pathing layer");
	std::printf("  reflections data size = %zu\n", (size_t)reflection_bytes);

	std::vector<uint8_t> baked_bytes;
	bool baked_save = bake_ok && reflections_ok && probe_core_save_batch(ctx, floor, &baked_bytes, &err);
	expect(baked_save && baked_bytes.size() >= bytes.size(), "serialize baked batch");

	IPLProbeBatch baked_loaded = nullptr;
	int baked_n = 0;
	bool baked_load = baked_save && probe_core_load_batch(ctx, baked_bytes.data(), baked_bytes.size(),
			&baked_loaded, &baked_n, &err);
	expect(baked_load && baked_n == floor_n, "reload baked batch");
	expect(baked_load && probe_core_pathing_data_size(baked_loaded) == path_bytes, "reloaded pathing data size matches");
	expect(baked_load && probe_core_reflections_data_size(baked_loaded) == reflection_bytes, "reloaded reflection data size matches");

	// Runtime pathing uses getInfluencingProbes, which needs iplProbeBatchCommit after load.
	for (int layers = 0; baked_load && box && layers < 4; ++layers) {
		// A fresh simulator per case prevents stale outputs from satisfying controls.
		IPLProbeBatch runtime_batch = nullptr;
		bool runtime_load = probe_core_load_batch(ctx, baked_bytes.data(), baked_bytes.size(),
				&runtime_batch, nullptr, &err);
		expect(runtime_load, "load independent runtime batch");
		if (!runtime_load) {
			continue;
		}
		IPLBakedDataIdentifier path_id{};
		path_id.type = IPL_BAKEDDATATYPE_PATHING;
		path_id.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
		IPLBakedDataIdentifier reflection_id{};
		reflection_id.type = IPL_BAKEDDATATYPE_REFLECTIONS;
		reflection_id.variation = IPL_BAKEDDATAVARIATION_REVERB;
		if (!(layers & 1)) {
			iplProbeBatchRemoveData(runtime_batch, &path_id);
		}
		if (!(layers & 2)) {
			iplProbeBatchRemoveData(runtime_batch, &reflection_id);
		}
		expect((probe_core_pathing_data_size(runtime_batch) > 0) == bool(layers & 1),
				"runtime path layer matches test case");
		expect((probe_core_reflections_data_size(runtime_batch) > 0) == bool(layers & 2),
				"runtime reflection layer matches test case");
		std::printf("  runtime layers: pathing=%d reflections=%d\n", bool(layers & 1), bool(layers & 2));
		IPLSimulationSettings sim_cfg{};
		sim_cfg.flags = static_cast<IPLSimulationFlags>(
				IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING);
		sim_cfg.sceneType = IPL_SCENETYPE_DEFAULT;
		sim_cfg.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		sim_cfg.maxNumOcclusionSamples = 4;
		sim_cfg.maxNumRays = 256;
		sim_cfg.numDiffuseSamples = 32;
		sim_cfg.maxDuration = 0.1f;
		sim_cfg.maxOrder = 1;
		sim_cfg.maxNumSources = 4;
		sim_cfg.numThreads = 1;
		sim_cfg.rayBatchSize = 1;
		sim_cfg.numVisSamples = 4;
		sim_cfg.samplingRate = 48000;
		sim_cfg.frameSize = 256;
		IPLSimulator simulator = nullptr;
		bool sim_ok = iplSimulatorCreate(ctx, &sim_cfg, &simulator) == IPL_STATUS_SUCCESS && simulator;
		expect(sim_ok, "create baked runtime simulator");
		if (sim_ok) {
			iplSimulatorSetScene(simulator, box);
			iplSimulatorAddProbeBatch(simulator, runtime_batch);
			iplSimulatorCommit(simulator);

			IPLSourceSettings src_cfg{};
			src_cfg.flags = static_cast<IPLSimulationFlags>(
					IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING);
			IPLSource src = nullptr;
			bool src_ok = iplSourceCreate(simulator, &src_cfg, &src) == IPL_STATUS_SUCCESS && src;
			expect(src_ok, "create baked runtime source");
			if (src_ok) {
				iplSourceAdd(src, simulator);
				iplSimulatorCommit(simulator);

				IPLCoordinateSpace3 coords{};
				coords.origin = IPLVector3{ -4.f, 1.75f, 0.f };
				coords.ahead = IPLVector3{ 0.f, 0.f, -1.f };
				coords.up = IPLVector3{ 0.f, 1.f, 0.f };
				coords.right = IPLVector3{ 1.f, 0.f, 0.f };

				IPLSimulationInputs reflection_inputs{};
				reflection_inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
				reflection_inputs.source = coords;
				reflection_inputs.baked = IPL_TRUE;
				reflection_inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
				reflection_inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
				for (float &scale : reflection_inputs.reverbScale) {
					scale = 1.f;
				}
				reflection_inputs.hybridReverbTransitionTime = 0.05f;
				reflection_inputs.hybridReverbOverlapPercent = 0.25f;
				iplSourceSetInputs(src, IPL_SIMULATIONFLAGS_REFLECTIONS, &reflection_inputs);

				IPLSimulationSharedInputs reflection_shared{};
				reflection_shared.listener = coords;
				reflection_shared.listener.origin = IPLVector3{ 4.f, 1.75f, 0.f };
				reflection_shared.numRays = 256;
				reflection_shared.numBounces = 4;
				reflection_shared.duration = 0.1f;
				reflection_shared.order = 1;
				reflection_shared.irradianceMinDistance = 1.f;
				iplSimulatorSetSharedInputs(simulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &reflection_shared);
				iplSimulatorRunReflections(simulator);
				IPLSimulationOutputs reflection_outputs{};
				iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_REFLECTIONS, &reflection_outputs);
				IPLAudioSettings audio{ 48000, 256 };
				IPLReflectionEffectSettings effect_cfg{ IPL_REFLECTIONEFFECTTYPE_CONVOLUTION, 4800, 4 };
				IPLReflectionEffect effect = nullptr;
				bool effect_ok = iplReflectionEffectCreate(ctx, &audio, &effect_cfg, &effect) == IPL_STATUS_SUCCESS && effect;
				expect(effect_ok, "create convolution renderer");
				if (effect_ok) {
					float input_samples[256]{};
					float output_samples[4][256]{};
					float *in_channels[] = { input_samples };
					float *out_channels[] = { output_samples[0], output_samples[1], output_samples[2], output_samples[3] };
					IPLAudioBuffer in{ 1, 256, in_channels };
					IPLAudioBuffer out{ 4, 256, out_channels };
					auto &params = reflection_outputs.reflections;
					params.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
					params.numChannels = 4;
					params.irSize = 4800;
					double energy = 0.0;
					double silent_energy = 0.0;
					double tail_energy = 0.0;
					bool finite = true;
					// Prime the IR crossfade with silence, then render a unit impulse
					// and the entire 0.1-second tail plus convolution latency.
					for (int frame = 0; frame < 24; ++frame) {
						input_samples[0] = frame == 2 ? 1.f : 0.f;
						iplReflectionEffectApply(effect, &params, &in, &out, nullptr);
						for (const auto &channel : output_samples) {
							for (float sample : channel) {
								finite = finite && std::isfinite(sample);
								double sample_energy = double(sample) * sample;
								if (frame < 2) {
									silent_energy += sample_energy;
								} else {
									energy += sample_energy;
								}
								if (frame > 2) {
									tail_energy += sample_energy;
								}
							}
						}
					}
					std::printf("  rendered reflection impulse energy = %.9g\n", energy);
					expect(finite, "rendered reflection samples are finite");
					expect(silent_energy < 1e-12, "silent input before impulse renders silence");
					expect((layers & 2) ? energy > 1e-8 : energy < 1e-12,
							(layers & 2) ? "baked reflections render impulse energy" : "absent reflection layer renders silence");
					if (layers & 2) {
						expect(tail_energy > 1e-8, "reflection impulse has a tail beyond the input frame");
					}
					iplReflectionEffectRelease(&effect);
				}

				IPLSimulationInputs inputs{};
				inputs.flags = IPL_SIMULATIONFLAGS_PATHING;
				inputs.source = coords;
				inputs.pathingProbes = runtime_batch;
				inputs.pathingOrder = 1;
				inputs.visRadius = 1.f;
				inputs.visThreshold = 0.5f;
				inputs.visRange = 20.f;
				inputs.enableValidation = IPL_TRUE;
				inputs.findAlternatePaths = IPL_FALSE;
				iplSourceSetInputs(src, IPL_SIMULATIONFLAGS_PATHING, &inputs);

				IPLSimulationSharedInputs shared{};
				shared.listener = coords;
				shared.listener.origin = IPLVector3{ 4.f, 1.75f, 0.f };
				iplSimulatorSetSharedInputs(simulator, IPL_SIMULATIONFLAGS_PATHING, &shared);

				iplSimulatorRunPathing(simulator);
				IPLSimulationOutputs outputs{};
				iplSourceGetOutputs(src, IPL_SIMULATIONFLAGS_PATHING, &outputs);
				expect(outputs.pathing.shCoeffs != nullptr, "RunPathing after load produces SH coeffs");
				if (outputs.pathing.shCoeffs) {
					double sh_energy = 0.0;
					bool finite = true;
					for (int i = 0; i < 4; ++i) {
						float sh = outputs.pathing.shCoeffs[i];
						finite = finite && std::isfinite(sh);
						sh_energy += double(sh) * sh;
					}
					std::printf("  path SH = [%.6g, %.6g, %.6g, %.6g], energy = %.9g\n",
							outputs.pathing.shCoeffs[0], outputs.pathing.shCoeffs[1],
							outputs.pathing.shCoeffs[2], outputs.pathing.shCoeffs[3], sh_energy);
					expect(finite, "path SH coefficients are finite");
					expect((layers & 1) ? sh_energy > 1e-8 : sh_energy < 1e-12,
							(layers & 1) ? "baked path carries energy around wall" : "absent path layer cannot route around wall");
					if (layers & 1) {
						expect(outputs.pathing.shCoeffs[0] > 0.f &&
								sh_energy - double(outputs.pathing.shCoeffs[0]) * outputs.pathing.shCoeffs[0] > 1e-8,
								"baked path has positive omnidirectional and nonzero directional SH");
					}
				}

				iplSourceRemove(src, simulator);
				iplSimulatorCommit(simulator);
				iplSourceRelease(&src);
			}
			iplSimulatorRemoveProbeBatch(simulator, runtime_batch);
			iplSimulatorCommit(simulator);
			iplSimulatorRelease(&simulator);
		}
		probe_core_release_batch(&runtime_batch);
	}

	probe_core_release_batch(&floor);
	probe_core_release_batch(&baked_loaded);
	probe_core_destroy_scene(&box);
	probe_core_destroy_scene(&empty);
	probe_core_destroy_context(&ctx);

	if (g_fails) {
		std::fprintf(stderr, "%d test(s) failed\n", g_fails);
		return 1;
	}
	std::printf("all tests passed\n");
	return 0;
}
