#include "probe_core.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char *message) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

static void expect_energy(double energy, bool audible, const char *message) {
	std::printf("%s: energy=%g\n", message, energy);
	// Allow the SDK's small reconstruction noise floor without accepting a
	// residual baked response. The floor fixture produces energy around 0.4.
	require(audible ? energy > 1e-8 : energy < 1e-10, message);
}

struct Runtime {
	IPLSimulator sim = nullptr;
	IPLSource source = nullptr;
	IPLReflectionEffect effect = nullptr;
	IPLSimulationInputs inputs{};
	IPLReflectionEffectParams published{};
	float dry[256]{};
	float wet[4][256]{};
	float *in_channels[1] = { dry };
	float *out_channels[4] = { wet[0], wet[1], wet[2], wet[3] };
	IPLAudioBuffer in{ 1, 256, in_channels };
	IPLAudioBuffer out{ 4, 256, out_channels };

	Runtime(IPLContext ctx, IPLScene scene, IPLProbeBatch batch) {
		IPLSimulationSettings settings{};
		settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT |
				IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING);
		settings.reflectionType = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		settings.maxNumOcclusionSamples = 4;
		settings.maxNumRays = 256;
		settings.numDiffuseSamples = 32;
		settings.maxDuration = 0.1f;
		settings.maxOrder = 1;
		settings.maxNumSources = 4;
		settings.numThreads = 1;
		settings.numVisSamples = 4;
		settings.rayBatchSize = 1;
		settings.samplingRate = 48000;
		settings.frameSize = 256;
		require(iplSimulatorCreate(ctx, &settings, &sim) == IPL_STATUS_SUCCESS, "create simulator");
		iplSimulatorSetScene(sim, scene);
		iplSimulatorAddProbeBatch(sim, batch);
		source = create_source();
		iplSourceAdd(source, sim);
		iplSimulatorCommit(sim);

		IPLAudioSettings audio{ 48000, 256 };
		IPLReflectionEffectSettings effect_settings{ IPL_REFLECTIONEFFECTTYPE_CONVOLUTION, 4800, 4 };
		require(iplReflectionEffectCreate(ctx, &audio, &effect_settings, &effect) == IPL_STATUS_SUCCESS,
				"create reflection effect");

		IPLSimulationSharedInputs shared{};
		shared.listener.origin = { -2, 1.75f, 0 };
		shared.listener.ahead = { 0, 0, -1 };
		shared.listener.up = { 0, 1, 0 };
		shared.listener.right = { 1, 0, 0 };
		shared.numRays = 256;
		shared.numBounces = 4;
		shared.duration = 0.1f;
		shared.order = 1;
		shared.irradianceMinDistance = 1;
		iplSimulatorSetSharedInputs(sim, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared);
		inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		inputs.source = shared.listener;
		inputs.source.origin = { 2, 1.75f, 0 };
		inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
		inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
		for (auto &scale : inputs.reverbScale) {
			scale = 1;
		}
	}

	IPLSource create_source() {
		IPLSourceSettings settings{};
		settings.flags = static_cast<IPLSimulationFlags>(IPL_SIMULATIONFLAGS_DIRECT |
				IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING);
		IPLSource result = nullptr;
		require(iplSourceCreate(sim, &settings, &result) == IPL_STATUS_SUCCESS, "create source");
		IPLSimulationInputs disabled{};
		iplSourceSetInputs(result, IPL_SIMULATIONFLAGS_PATHING, &disabled);
		return result;
	}

	void replace_source() {
		// Mirror server.cpp's idle transition: unpublish borrowed pointers,
		// reset input history, then replace the source owning the IR/cache.
		published = {};
		iplReflectionEffectReset(effect);
		IPLSource replacement = create_source();
		iplSourceRemove(source, sim);
		iplSourceAdd(replacement, sim);
		iplSimulatorCommit(sim);
		iplSourceRelease(&source);
		source = replacement;
	}

	void run(bool baked) {
		inputs.baked = baked ? IPL_TRUE : IPL_FALSE;
		iplSourceSetInputs(source, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
		iplSimulatorRunReflections(sim);
		IPLSimulationOutputs outputs{};
		iplSourceGetOutputs(source, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
		published = outputs.reflections;
		published.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
	}

	double apply() {
		require(published.ir != nullptr, "published reflection IR");
		iplReflectionEffectApply(effect, &published, &in, &out, nullptr);
		double energy = 0;
		for (const auto &channel : wet) {
			for (float sample : channel) {
				require(std::isfinite(sample), "finite reflection samples");
				energy += double(sample) * sample;
			}
		}
		return energy;
	}

	double render(bool impulse) {
		double energy = 0;
		// Cover the entire 0.1-second IR, with one silent priming frame.
		// Do not reset here: that would hide a missing transition reset.
		for (int frame = 0; frame < 24; ++frame) {
			std::memset(dry, 0, sizeof(dry));
			if (impulse && frame == 1) {
				dry[0] = 1;
			}
			energy += apply();
		}
		return energy;
	}

	~Runtime() {
		iplReflectionEffectRelease(&effect);
		iplSourceRelease(&source);
		iplSimulatorRelease(&sim);
	}
};

int main() {
	std::string error;
	IPLContext ctx = probe_core_create_context(&error);
	require(ctx != nullptr, error.c_str());
	IPLScene bake_scene = probe_core_create_box_scene(ctx, 8, 0.25f, 8, &error);
	IPLScene empty_scene = probe_core_create_empty_scene(ctx, &error);
	require(bake_scene && empty_scene, "create scenes");
	IPLProbeBatch batch = nullptr;
	require(iplProbeBatchCreate(ctx, &batch) == IPL_STATUS_SUCCESS, "create probe batch");
	iplProbeBatchAddProbe(batch, { { -2, 1.75f, 0 }, 10 });
	iplProbeBatchCommit(batch);
	require(probe_core_bake_reflections(ctx, bake_scene, batch,
			256, 32, 4, 0.1f, 0.1f, 1, 1, 1, 1, &error), "bake floor reflections");

	// RT uses an empty scene; baked lookup uses the reflective floor. Keep
	// scene and transforms fixed so movement cannot reset accumulated state.
	for (bool first_baked : { false, true }) {
		Runtime runtime(ctx, empty_scene, batch);
		runtime.run(first_baked); // Leave its IR pending, without any Apply call.
		runtime.replace_source();
		runtime.run(!first_baked);
		expect_energy(runtime.render(true), !first_baked,
				first_baked ? "pending baked -> RT uses new RT IR" : "pending RT -> baked uses new baked IR");
	}

	{
		Runtime runtime(ctx, empty_scene, batch);
		for (int frame = 0; frame < 8; ++frame) {
			runtime.run(false);
		}
		expect_energy(runtime.render(true), false, "warm RT accumulation is silent");
		// Seed both cached RT frames and baked energy in a reused SDK source.
		// Consume the baked IR so this case tests accumulation, not pending IRs.
		runtime.run(true);
		expect_energy(runtime.render(true), true, "cached source has baked energy");
		runtime.replace_source();
		runtime.run(false);
		expect_energy(runtime.render(true), false, "recreation discards accumulated baked energy");
	}

	for (bool first_baked : { false, true }) {
		Runtime runtime(ctx, empty_scene, batch);
		runtime.run(first_baked);
		for (auto &sample : runtime.dry) {
			sample = 1;
		}
		for (int frame = 0; frame < 8; ++frame) {
			runtime.apply(); // Populate input history, even when the old IR is silent.
		}
		runtime.replace_source();
		runtime.run(!first_baked);
		expect_energy(runtime.render(false), false,
				first_baked ? "baked -> RT discards old input history" : "RT -> baked discards old input history");
		expect_energy(runtime.render(true), !first_baked, "new input renders only the destination mode");
	}

	iplProbeBatchRelease(&batch);
	probe_core_destroy_scene(&bake_scene);
	probe_core_destroy_scene(&empty_scene);
	probe_core_destroy_context(&ctx);
	std::puts("PASS: reflection transitions reset pending IRs, accumulation, and input history");
}
