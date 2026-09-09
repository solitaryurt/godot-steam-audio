#include "server.hpp"
#include "config.hpp"
#include "probe_core.hpp"
#include <cstdio>
#include <cstring>
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/memory.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "phonon.h"
#include "player.hpp"
#include "server_init.hpp"
#include "steam_audio.hpp"
#include <algorithm>
#include <cmath>
#include <godot_cpp/variant/utility_functions.hpp>

void SteamAudioServer::wait_for_refl_idle() {
	// Caller must hold tick_mux so tick() cannot start a new reflection run.
	if (!is_refl_thread_processing.load()) {
		return;
	}
	std::unique_lock<std::mutex> lock(refl_mux);
	refl_idle_cv.wait(lock, [&] { return !is_refl_thread_processing.load(); });
}

void SteamAudioServer::apply_pending_scene_ops() {
	for (auto m : static_meshes_to_add) {
		iplStaticMeshAdd(m, global_state.scene);
	}
	static_meshes_to_add.clear();

	for (auto m : dynamic_meshes_to_add) {
		iplInstancedMeshAdd(m, global_state.scene);
	}
	dynamic_meshes_to_add.clear();

	for (auto &kv : pending_transforms) {
		iplInstancedMeshUpdateTransform(kv.first, global_state.scene, kv.second);
	}
	pending_transforms.clear();
}

bool SteamAudioServer::is_probe_visible(IPLVector3 point, IPLVector3 probe) {
	// tick_mux is held and the reflection worker is idle. Use the same SDK
	// scene and ray segment as ProbeNeighborhood::checkOcclusion (4.5.3).
	if (!visibility_source) {
		return false;
	}
	IPLSimulationSharedInputs shared{};
	shared.listener = ipl_coords_from(Transform3D());
	shared.listener.origin = point;
	iplSimulatorSetSharedInputs(visibility_sim, IPL_SIMULATIONFLAGS_DIRECT, &shared);
	IPLSimulationInputs inputs{};
	inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
	inputs.directFlags = IPL_DIRECTSIMULATIONFLAGS_OCCLUSION;
	inputs.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
	inputs.source = shared.listener;
	inputs.source.origin = probe;
	iplSourceSetInputs(visibility_source, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
	iplSimulatorRunDirect(visibility_sim);
	IPLSimulationOutputs outputs{};
	iplSourceGetOutputs(visibility_source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
	return outputs.direct.occlusion == 1.0f;
}

bool SteamAudioServer::has_visible_probes(const ProbeBatchEntry &entry, IPLVector3 point) {
	return probe_core_query_neighborhood(entry.probes, point,
			[this](IPLVector3 from, IPLVector3 to) { return is_probe_visible(from, to); })
			.has_guaranteed_visible_probe;
}

bool SteamAudioServer::can_use_baked_reverb(IPLVector3 point) {
	bool visible = false;
	for (const auto &entry : probe_batches) {
		// Unknown spheres cannot be ruled out of the SDK's weight sum.
		if (entry.probes.empty()) {
			return false;
		}
		auto neighborhood = probe_core_query_neighborhood(entry.probes, point,
				[this](IPLVector3 from, IPLVector3 to) { return is_probe_visible(from, to); });
		if (!entry.has_reflections && neighborhood.has_visible_probe) {
			// Visible unbaked probes enter the SDK lookup: 4.5.3 dilutes weights,
			// while 4.8.1 can use their indices in another batch's reflection data.
			return false;
		}
		visible = (entry.has_reflections && neighborhood.has_guaranteed_visible_probe) || visible;
	}
	return visible;
}

void SteamAudioServer::tick() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (!self->is_global_state_init.load()) {
		return;
	}
	std::lock_guard<std::mutex> tick_lock(self->tick_mux);
	if (self->listener == nullptr || !self->listener->is_inside_tree()) {
		return;
	}

	SteamAudio::log(SteamAudio::log_debug, "tick");

	if (!is_refl_thread_processing.load()) {
		apply_pending_scene_ops();
		iplSceneCommit(self->global_state.scene);
	}

	SteamAudio::log(SteamAudio::log_debug, "tick: committed scene");

	self->global_state.listener_coords =
			ipl_coords_from(self->listener->get_global_transform());

	for (auto ls : self->local_states) {
		if (ls->src.player == nullptr || !ls->src.player->is_inside_tree()) {
			continue;
		}
		if (!ls->src.player->is_playing()) {
			continue;
		}

		Vector3 src_pos = ls->src.player->get_global_position();
		Vector3 dir = src_pos - self->listener->get_global_position();
		SteamAudioSourceConfig cfg_copy;
		{
			std::unique_lock lock(ls->mux);
			ls->dir_to_listener = dir;
			ls->listener_coords = global_state.listener_coords;
			cfg_copy = ls->cfg;
		}

		IPLDistanceAttenuationModel attn_model{};
		attn_model.type = IPL_DISTANCEATTENUATIONTYPE_INVERSEDISTANCE;
		attn_model.minDistance = cfg_copy.min_attn_dist;

		IPLAirAbsorptionModel absorp_model{};
		absorp_model.type = cfg_copy.air_absorption_model_type;
		absorp_model.coefficients[0] = cfg_copy.air_absorption_low;
		absorp_model.coefficients[1] = cfg_copy.air_absorption_mid;
		absorp_model.coefficients[2] = cfg_copy.air_absorption_high;

		IPLCoordinateSpace3 src_coords = ipl_coords_from(ls->src.player->get_global_transform());

		IPLSimulationInputs inputs{};
		inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
		inputs.distanceAttenuationModel = attn_model;
		inputs.airAbsorptionModel = absorp_model;
		inputs.source = src_coords;
		inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
		inputs.occlusionRadius = cfg_copy.occ_radius;
		inputs.numOcclusionSamples = cfg_copy.occ_samples;
		inputs.numTransmissionRays = cfg_copy.transm_rays;

		if (cfg_copy.is_air_absorp_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION);
		}

		if (cfg_copy.is_dist_attn_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_DISTANCEATTENUATION);
		}
		if (cfg_copy.is_occlusion_on) {
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_OCCLUSION |
					IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION);
		}
		if (cfg_copy.is_directivity_on) {
			inputs.directivity.dipoleWeight = cfg_copy.dipole_weight;
			inputs.directivity.dipolePower = cfg_copy.dipole_power;
			inputs.directFlags = static_cast<IPLDirectSimulationFlags>(
					inputs.directFlags |
					IPL_DIRECTSIMULATIONFLAGS_DIRECTIVITY);
		}

		SteamAudio::log(SteamAudio::log_debug, "tick: setting inputs");
		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
	}
	SteamAudio::log(SteamAudio::log_debug, "tick: direct inputs set");

	IPLSimulationSharedInputs shared_inputs{};
	shared_inputs.listener = self->global_state.listener_coords;
	iplSimulatorSetSharedInputs(self->global_state.sim,
			IPL_SIMULATIONFLAGS_DIRECT, &shared_inputs);
	iplSimulatorRunDirect(self->global_state.sim);

	SteamAudio::log(SteamAudio::log_debug, "tick: direct sim complete");

	for (auto ls : self->local_states) {
		if (ls->src.player == nullptr || !ls->src.player->is_inside_tree()) {
			continue;
		}
		if (!ls->src.player->is_playing()) {
			continue;
		}

		IPLSimulationOutputs outputs{};
		iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_DIRECT, &outputs);
		{
			std::unique_lock lock(ls->mux);
			ls->direct_outputs = outputs.direct;
		}
	}

	if (is_refl_thread_processing.load()) {
		SteamAudio::log(SteamAudio::log_debug, "tick: done, skipping reflections");
		return;
	}

	bool checked_baked_reverb = false;
	bool baked_reverb_available = false;
	for (auto ls : self->local_states) {
		std::unique_lock lock(ls->mux);
		const auto &cfg = ls->cfg;
		bool enabled = cfg.is_reflection_on && ls->src.player != nullptr &&
				ls->src.player->is_inside_tree() && ls->src.player->is_playing() &&
				ls->src.player->get_global_position().distance_to(listener->get_global_position()) <= cfg.max_refl_dist;
		if (enabled && !checked_baked_reverb) {
			baked_reverb_available = can_use_baked_reverb(global_state.listener_coords.origin);
			checked_baked_reverb = true;
		}
		bool baked = enabled && baked_reverb_available;
		{
			std::lock_guard ir_lock(global_state.refl_ir_lock);
			ls->refl_outputs = {};
			if (baked != ls->refl_simulation_baked) {
				// Clearing the published IR does not consume the SDK's pending
				// triple buffer or reset its accumulated energy. Replace both.
				// tick_mux + idle worker + mux + refl_ir_lock exclude all users.
				ls->refl_simulation_pending = false;
				iplReflectionEffectReset(ls->fx.refl);
				iplAmbisonicsDecodeEffectReset(ls->fx.refl_dec);
				IPLSourceSettings settings{};
				settings.flags = static_cast<IPLSimulationFlags>(
						IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS | IPL_SIMULATIONFLAGS_PATHING);
				IPLSource replacement = nullptr;
				IPLSimulationInputs disabled{};
				if (!handleErr(iplSourceCreate(global_state.sim, &settings, &replacement), "reflection mode source") || !replacement) {
					// Keep the old handle valid for direct/pathing and retry next
					// tick, but never publish its IR under the requested mode.
					iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &disabled);
					continue;
				}
				iplSourceSetInputs(replacement, IPL_SIMULATIONFLAGS_PATHING, &disabled);
				ls->path_simulation_order = -1;
				ls->path_outputs = {};
				memset(ls->path_sh_coeffs, 0, sizeof(ls->path_sh_coeffs));
				iplSourceRemove(ls->src.src, global_state.sim);
				iplSourceAdd(replacement, global_state.sim);
				iplSimulatorCommit(global_state.sim);
				iplSourceRelease(&ls->src.src);
				ls->src.src = replacement;
				// Direct outputs are values from this tick, not borrowed pointers.
				// Keep them until direct inputs are restored next tick. Pathing
				// inputs are restored below, before the worker can run again.
			}
			if (enabled && ls->refl_simulation_pending) {
				IPLSimulationOutputs outputs{};
				iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
				ls->refl_outputs = outputs.reflections;
				ls->refl_outputs_baked = ls->refl_simulation_baked;
			}
		}
		IPLSimulationInputs inputs{};
		if (enabled) {
			inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
			inputs.source = ipl_coords_from(ls->src.player->get_global_transform());
			inputs.baked = baked ? IPL_TRUE : IPL_FALSE;
			inputs.bakedDataIdentifier.type = IPL_BAKEDDATATYPE_REFLECTIONS;
			inputs.bakedDataIdentifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
		}
		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
		ls->refl_simulation_pending = enabled;
		ls->refl_simulation_baked = baked;
	}

	const ProbeBatchEntry *pathing_batch = nullptr;
	for (const auto &entry : probe_batches) {
		if (entry.has_pathing) {
			pathing_batch = &entry;
			break;
		}
	}
	bool checked_path_listener = false;
	bool path_listener_valid = false;
	for (auto ls : self->local_states) {
		std::unique_lock lock(ls->mux);
		const auto &cfg = ls->cfg;
		bool enabled = pathing_batch != nullptr && ls->src.player != nullptr &&
				ls->src.player->is_inside_tree() && ls->src.player->is_playing();
		if (enabled) {
			IPLVector3 source = ipl_vec3_from(ls->src.player->get_global_position());
			IPLVector3 point = global_state.listener_coords.origin;
			if (!is_probe_visible(point, source)) {
				if (!checked_path_listener) {
					path_listener_valid = has_visible_probes(*pathing_batch, point);
					checked_path_listener = true;
				}
				// SDK 4.5.3 leaves cached SH untouched on a missing neighborhood.
				// Submit only runs that will actually write the requested order.
				enabled = path_listener_valid && has_visible_probes(*pathing_batch, source);
			}
		}
		ls->path_outputs = {};
		memset(ls->path_sh_coeffs, 0, sizeof(ls->path_sh_coeffs));
		if (enabled && ls->path_simulation_order >= 0) {
			IPLSimulationOutputs outputs{};
			iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_PATHING, &outputs);
			if (outputs.pathing.shCoeffs != nullptr) {
				// SDK 4.5.3 does not populate pathing.order. Use the completed
				// run's input order, not the possibly changed player setting.
				int order = ls->path_simulation_order;
				memcpy(ls->path_sh_coeffs, outputs.pathing.shCoeffs,
						size_t(ambisonic_channels_from(order)) * sizeof(float));
				ls->path_outputs = outputs.pathing;
				ls->path_outputs.order = order;
				ls->path_outputs.shCoeffs = ls->path_sh_coeffs;
			}
		}
		IPLSimulationInputs inputs{};
		ls->path_simulation_order = -1;
		if (!enabled) {
			// Skipping SetInputs leaves the source enabled inside the SDK.
			iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_PATHING, &inputs);
			continue;
		}
		inputs.flags = IPL_SIMULATIONFLAGS_PATHING;
		inputs.source = ipl_coords_from(ls->src.player->get_global_transform());
		inputs.pathingProbes = pathing_batch->batch;
		inputs.pathingOrder = std::clamp(cfg.pathing_order, 0, std::clamp(global_state.sim_cfg.maxOrder, 0, 5));
		inputs.visRadius = cfg.vis_radius;
		inputs.visThreshold = cfg.vis_threshold;
		inputs.visRange = cfg.vis_range;
		inputs.enableValidation = cfg.pathing_validation ? IPL_TRUE : IPL_FALSE;
		inputs.findAlternatePaths = cfg.pathing_find_alternate ? IPL_TRUE : IPL_FALSE;
		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_PATHING, &inputs);
		ls->path_simulation_order = inputs.pathingOrder;
	}

	if (listener == nullptr || !listener->is_inside_tree()) {
		return;
	}
	shared_inputs = IPLSimulationSharedInputs{};
	shared_inputs.listener = global_state.listener_coords;
	shared_inputs.numRays = listener->get_num_refl_rays();
	shared_inputs.numBounces = listener->get_num_refl_bounces();
	shared_inputs.duration = listener->get_refl_duration();
	shared_inputs.order = std::clamp(listener->get_refl_ambisonics_order(), 0, global_state.sim_cfg.maxOrder);
	shared_inputs.irradianceMinDistance = listener->get_irradiance_min_dist();
	iplSimulatorSetSharedInputs(global_state.sim, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared_inputs);
	iplSimulatorSetSharedInputs(global_state.sim, IPL_SIMULATIONFLAGS_PATHING, &shared_inputs);

	{
		// notify reflection thread and tell it it can start running again
		std::unique_lock<std::mutex> lock(refl_mux);
		is_refl_thread_processing.store(true);
		cv.notify_one();
	}

	SteamAudio::log(SteamAudio::log_debug, "tick: done");
}

GlobalSteamAudioState *SteamAudioServer::get_global_state(bool should_init) {
	self->init_mux.lock();
	if (self->is_global_state_init.load()) {
		self->init_mux.unlock();
		return &self->global_state;
	}

	if (!should_init) {
		self->init_mux.unlock();
		return nullptr;
	}

	SteamAudio::log(SteamAudio::log_info, "Initializing SteamAudioServer global state");

	global_state.audio_cfg = create_audio_cfg();
	global_state.ctx = create_ctx();
	if (global_state.ctx == nullptr) {
		init_mux.unlock();
		return nullptr;
	}

	IPLSceneSettings scene_cfg = create_scene_cfg(global_state.ctx);
	if (!handleErr(iplSceneCreate(global_state.ctx, &scene_cfg, &global_state.scene), "iplSceneCreate") ||
			global_state.scene == nullptr) {
		init_mux.unlock();
		return nullptr;
	}
	for (auto m : static_meshes_to_add) {
		iplStaticMeshAdd(m, global_state.scene);
	}
	static_meshes_to_add.clear();

	// Keep path simulation and effect bounds tied to the actual allocation.
	global_state.sim_cfg.maxOrder = SteamAudioConfig::max_ambisonics_order;
	global_state.sim = create_simulator(
			global_state.ctx, global_state.audio_cfg, scene_cfg);
	global_state.hrtf = create_hrtf(global_state.ctx, global_state.audio_cfg);
	global_state.ambi_enc_effect = create_ambisonics_encode_effect(
			global_state.ctx, global_state.audio_cfg);
	global_state.ambi_dec_effect = create_ambisonics_decode_effect(
			global_state.ctx, global_state.audio_cfg, global_state.hrtf);
	if (global_state.sim == nullptr || global_state.hrtf == nullptr ||
			global_state.ambi_enc_effect == nullptr || global_state.ambi_dec_effect == nullptr) {
		init_mux.unlock();
		return nullptr;
	}

	iplSimulatorSetScene(global_state.sim, global_state.scene);
	iplSimulatorCommit(global_state.sim);

	IPLSimulationSettings visibility_cfg{};
	visibility_cfg.flags = IPL_SIMULATIONFLAGS_DIRECT;
	visibility_cfg.sceneType = scene_cfg.type;
	visibility_cfg.maxNumOcclusionSamples = 1;
	visibility_cfg.samplingRate = global_state.audio_cfg.samplingRate;
	visibility_cfg.frameSize = global_state.audio_cfg.frameSize;
	if (handleErr(iplSimulatorCreate(global_state.ctx, &visibility_cfg, &visibility_sim), "probe visibility simulator")) {
		IPLSourceSettings source_cfg{};
		source_cfg.flags = IPL_SIMULATIONFLAGS_DIRECT;
		if (handleErr(iplSourceCreate(visibility_sim, &source_cfg, &visibility_source), "probe visibility source")) {
			iplSimulatorSetScene(visibility_sim, global_state.scene);
			iplSourceAdd(visibility_source, visibility_sim);
			iplSimulatorCommit(visibility_sim);
		}
	}

	is_global_state_init.store(true);
	init_mux.unlock();

	SteamAudio::log(SteamAudio::log_info, "Initialized SteamAudioServer global state");
	start_refl_sim();
	return &global_state;
}

void SteamAudioServer::start_refl_sim() {
	refl_thread.instantiate();
	refl_thread->start(callable_mp(this, &SteamAudioServer::run_refl_sim));
}

void SteamAudioServer::run_refl_sim() {
	while (this->is_running.load()) {
		{
			std::unique_lock<std::mutex> lock(this->refl_mux);
			cv.wait(lock, [&] { return is_refl_thread_processing.load() || !is_running.load(); });
		}
		if (!is_running.load()) {
			break;
		}
		SteamAudio::log(SteamAudio::log_debug, "running reflection sim");
		iplSimulatorRunReflections(global_state.sim);
		bool has_pathing = false;
		for (const auto &entry : probe_batches) {
			has_pathing = has_pathing || entry.has_pathing;
		}
		if (has_pathing) {
			SteamAudio::log(SteamAudio::log_debug, "running pathing sim");
			iplSimulatorRunPathing(global_state.sim);
		}
		{
			std::unique_lock<std::mutex> lock(this->refl_mux);
			is_refl_thread_processing.store(false);
			refl_idle_cv.notify_all();
		}
	}
}

void SteamAudioServer::add_listener(SteamAudioListener *lis) {
	std::lock_guard<std::mutex> lock(self->tick_mux);
	self->listener = lis;
}

void SteamAudioServer::add_local_state(LocalSteamAudioState *ls) {
	std::lock_guard<std::mutex> lock(self->tick_mux);
	wait_for_refl_idle();
	{
		std::unique_lock lock(ls->mux);
		IPLSimulationInputs inputs{};
		iplSourceSetInputs(ls->src.src, static_cast<IPLSimulationFlags>(
				IPL_SIMULATIONFLAGS_PATHING | IPL_SIMULATIONFLAGS_REFLECTIONS), &inputs);
		ls->path_simulation_order = -1;
		ls->path_outputs = {};
		ls->refl_simulation_pending = false;
		ls->refl_outputs = {};
	}
	iplSourceAdd(ls->src.src, global_state.sim);
	iplSimulatorCommit(global_state.sim);
	self->local_states.push_back(ls);
}

void SteamAudioServer::remove_local_state(LocalSteamAudioState *ls) {
	std::lock_guard<std::mutex> lock(tick_mux);
	auto it = std::find(local_states.begin(), local_states.end(), ls);
	if (it == local_states.end()) {
		return;
	}
	wait_for_refl_idle();
	iplSourceRemove(ls->src.src, global_state.sim);
	iplSimulatorCommit(global_state.sim);
	local_states.erase(it);
}

void SteamAudioServer::add_static_mesh(IPLStaticMesh mesh) {
	std::lock_guard<std::mutex> lock(tick_mux);
	static_meshes_to_add.push_back(mesh);
}

void SteamAudioServer::remove_static_mesh(IPLStaticMesh mesh) {
	std::lock_guard<std::mutex> lock(tick_mux);
	auto add = std::find(static_meshes_to_add.begin(), static_meshes_to_add.end(), mesh);
	if (add != static_meshes_to_add.end()) {
		static_meshes_to_add.erase(add);
		return;
	}
	if (!is_global_state_init.load()) {
		return;
	}
	wait_for_refl_idle();
	iplStaticMeshRemove(mesh, global_state.scene);
	iplSceneCommit(global_state.scene);
}

void SteamAudioServer::add_dynamic_mesh(IPLInstancedMesh mesh) {
	std::lock_guard<std::mutex> lock(tick_mux);
	if (!is_global_state_init.load()) {
		SteamAudio::log(SteamAudio::log_error, "Adding a dynamic mesh, but SteamAudio is not initialized. Probably crashing soon.");
		return;
	}
	dynamic_meshes_to_add.push_back(mesh);
}

void SteamAudioServer::remove_dynamic_mesh(IPLInstancedMesh mesh) {
	std::lock_guard<std::mutex> lock(tick_mux);
	if (mesh == nullptr) {
		return;
	}
	pending_transforms.erase(mesh);
	auto add = std::find(dynamic_meshes_to_add.begin(), dynamic_meshes_to_add.end(), mesh);
	if (add != dynamic_meshes_to_add.end()) {
		dynamic_meshes_to_add.erase(add);
		return;
	}
	if (!is_global_state_init.load()) {
		return;
	}
	wait_for_refl_idle();
	iplInstancedMeshRemove(mesh, global_state.scene);
	iplSceneCommit(global_state.scene);
}

void SteamAudioServer::update_dynamic_mesh_transform(IPLInstancedMesh mesh, IPLMatrix4x4 transform) {
	std::lock_guard<std::mutex> lock(tick_mux);
	if (!is_global_state_init.load() || mesh == nullptr) {
		return;
	}
	pending_transforms[mesh] = transform;
}

IPLProbeBatch SteamAudioServer::add_probe_batch(const uint8_t *data, size_t size, bool has_pathing, bool has_reflections,
		const std::vector<IPLSphere> &probes) {
	std::lock_guard<std::mutex> lock(tick_mux);
	if (!is_global_state_init.load() || data == nullptr || size == 0) {
		return nullptr;
	}
	wait_for_refl_idle();
	std::string err;
	IPLProbeBatch batch = nullptr;
	int count = 0;
	if (!probe_core_load_batch(global_state.ctx, data, size, &batch, &count, &err)) {
		SteamAudio::log(SteamAudio::log_error, err.c_str());
		return nullptr;
	}
	has_pathing = has_pathing && probe_core_pathing_data_size(batch) > 0;
	has_reflections = has_reflections && probe_core_reflections_data_size(batch) > 0;
	bool valid_probes = probes.size() == size_t(count);
	for (const auto &probe : probes) {
		valid_probes = valid_probes && std::isfinite(probe.center.x) && std::isfinite(probe.center.y) &&
				std::isfinite(probe.center.z) && std::isfinite(probe.radius) && probe.radius > 0.0f;
	}
	iplSimulatorAddProbeBatch(global_state.sim, batch);
	iplSimulatorCommit(global_state.sim);
	probe_batches.push_back({ batch, has_pathing, has_reflections, valid_probes ? probes : std::vector<IPLSphere>{} });
	char msg[96];
	snprintf(msg, sizeof(msg), "Loaded probe batch (%d probes)", count);
	SteamAudio::log(SteamAudio::log_info, msg);
	return batch;
}

void SteamAudioServer::remove_probe_batch(IPLProbeBatch batch) {
	std::lock_guard<std::mutex> lock(tick_mux);
	if (batch == nullptr || !is_global_state_init.load()) {
		return;
	}
	wait_for_refl_idle();
	auto it = std::find_if(probe_batches.begin(), probe_batches.end(),
			[batch](const ProbeBatchEntry &entry) { return entry.batch == batch; });
	if (it == probe_batches.end()) {
		return;
	}
	// Reset every source, including stopped players, before Commit destroys
	// the path simulator. Active players select a remaining batch next tick.
	for (auto ls : local_states) {
		std::unique_lock lock(ls->mux);
		IPLSimulationInputs inputs{};
		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_PATHING, &inputs);
		ls->path_simulation_order = -1;
		ls->path_outputs = {};
		memset(ls->path_sh_coeffs, 0, sizeof(ls->path_sh_coeffs));
	}
	probe_batches.erase(it);
	iplSimulatorRemoveProbeBatch(global_state.sim, batch);
	iplSimulatorCommit(global_state.sim);
	iplProbeBatchRelease(&batch);
}

SteamAudioServer::SteamAudioServer() {
	self = this;
	is_global_state_init.store(false);
	is_refl_thread_processing.store(false);
	is_running.store(true);
}

SteamAudioServer::~SteamAudioServer() {
	is_running.store(false);
	{
		std::unique_lock<std::mutex> lock(refl_mux);
		cv.notify_one();
		refl_idle_cv.notify_all();
	}
	if (refl_thread.is_valid()) {
		refl_thread->wait_to_finish();
		refl_thread.unref();
	}

	if (!self->is_global_state_init.load()) {
		return;
	}
	SteamAudio::log(SteamAudio::log_debug, "destroying steam audio server");

	for (auto &entry : probe_batches) {
		if (entry.batch) {
			iplSimulatorRemoveProbeBatch(global_state.sim, entry.batch);
			iplProbeBatchRelease(&entry.batch);
		}
	}
	probe_batches.clear();
	iplSourceRelease(&visibility_source);
	iplSimulatorRelease(&visibility_sim);

	iplAmbisonicsDecodeEffectRelease(&self->global_state.ambi_dec_effect);
	iplAmbisonicsEncodeEffectRelease(&self->global_state.ambi_enc_effect);
	iplHRTFRelease(&self->global_state.hrtf);
	iplSimulatorRelease(&self->global_state.sim);
	iplSceneRelease(&self->global_state.scene);
	iplContextRelease(&self->global_state.ctx);
}

void SteamAudioServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("tick"), &SteamAudioServer::tick);
	ClassDB::bind_static_method("SteamAudioServer", D_METHOD("get_singleton"),
			&SteamAudioServer::get_singleton);
}

SteamAudioServer *SteamAudioServer::get_singleton() {
	return self;
}

SteamAudioServer *SteamAudioServer::self = nullptr;
