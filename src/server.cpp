#include "server.hpp"
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

	for (auto ls : local_states) {
		if (ls->src.player == nullptr || !ls->src.player->is_inside_tree()) {
			continue;
		}
		if (!ls->src.player->is_playing()) {
			continue;
		}
		if (listener == nullptr || !listener->is_inside_tree()) {
			continue;
		}

		float max_refl_dist;
		{
			std::unique_lock lock(ls->mux);
			max_refl_dist = ls->cfg.max_refl_dist;
		}
		if (ls->src.player->get_global_position().distance_to(listener->get_global_position()) > max_refl_dist) {
			continue;
		}

		IPLSimulationOutputs outputs{};
		iplSourceGetOutputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);
		{
			std::unique_lock lock(ls->mux);
			std::lock_guard ir_lock(global_state.refl_ir_lock);
			ls->refl_outputs = outputs.reflections;
		}
	}

	for (auto ls : self->local_states) {
		if (ls->src.player == nullptr || !ls->src.player->is_inside_tree()) {
			continue;
		}
		if (!ls->src.player->is_playing()) {
			continue;
		}
		if (listener == nullptr || !listener->is_inside_tree()) {
			continue;
		}
		float max_refl_dist;
		{
			std::unique_lock lock(ls->mux);
			max_refl_dist = ls->cfg.max_refl_dist;
		}
		if (ls->src.player->get_global_position().distance_to(listener->get_global_position()) > max_refl_dist) {
			continue;
		}

		auto player = dynamic_cast<SteamAudioPlayer *>(ls->src.player);
		if (player == nullptr || !player->is_reflection_on()) {
			continue;
		}

		IPLCoordinateSpace3 src_coords = ipl_coords_from(ls->src.player->get_global_transform());

		IPLSimulationInputs inputs{};
		inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
		inputs.source = src_coords;

		iplSourceSetInputs(ls->src.src, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
	}

	if (listener == nullptr || !listener->is_inside_tree()) {
		return;
	}
	shared_inputs = IPLSimulationSharedInputs{};
	shared_inputs.listener = global_state.listener_coords;
	shared_inputs.numRays = listener->get_num_refl_rays();
	shared_inputs.numBounces = listener->get_num_refl_bounces();
	shared_inputs.duration = listener->get_refl_duration();
	shared_inputs.order = listener->get_refl_ambisonics_order();
	shared_inputs.irradianceMinDistance = listener->get_irradiance_min_dist();
	iplSimulatorSetSharedInputs(global_state.sim, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared_inputs);

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

	IPLSceneSettings scene_cfg = create_scene_cfg(global_state.ctx);
	IPLerror err = iplSceneCreate(global_state.ctx, &scene_cfg, &global_state.scene);
	handleErr(err);
	for (auto m : static_meshes_to_add) {
		iplStaticMeshAdd(m, global_state.scene);
	}
	static_meshes_to_add.clear();

	global_state.sim = create_simulator(
			global_state.ctx, global_state.audio_cfg, scene_cfg);
	global_state.hrtf = create_hrtf(global_state.ctx, global_state.audio_cfg);
	global_state.ambi_enc_effect = create_ambisonics_encode_effect(
			global_state.ctx, global_state.audio_cfg);
	global_state.ambi_dec_effect = create_ambisonics_decode_effect(
			global_state.ctx, global_state.audio_cfg, global_state.hrtf);

	iplSimulatorSetScene(global_state.sim, global_state.scene);
	iplSimulatorCommit(global_state.sim);

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
		// if someone removed a local state, then the reflection sim might crash, so
		// we need it to wait for another tick.
		if (local_states_have_changed.load()) {
			local_states_have_changed.store(false);
			{
				std::unique_lock<std::mutex> lock(this->refl_mux);
				is_refl_thread_processing.store(false);
				refl_idle_cv.notify_all();
			}
			continue;
		}
		SteamAudio::log(SteamAudio::log_debug, "running reflection sim");
		iplSimulatorRunReflections(global_state.sim);
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
	self->local_states.push_back(ls);
}

void SteamAudioServer::remove_local_state(LocalSteamAudioState *ls) {
	std::lock_guard<std::mutex> lock(tick_mux);
	auto it = std::find(local_states.begin(), local_states.end(), ls);
	if (it == local_states.end()) {
		return;
	}
	local_states.erase(it);
	local_states_have_changed.store(true);
}

void SteamAudioServer::add_source(IPLSource src) {
	std::lock_guard<std::mutex> lock(tick_mux);
	wait_for_refl_idle();
	iplSourceAdd(src, global_state.sim);
	iplSimulatorCommit(global_state.sim);
}

void SteamAudioServer::remove_source(IPLSource src) {
	std::lock_guard<std::mutex> lock(tick_mux);
	wait_for_refl_idle();
	iplSourceRemove(src, global_state.sim);
	iplSimulatorCommit(global_state.sim);
	local_states_have_changed.store(true);
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

SteamAudioServer::SteamAudioServer() {
	self = this;
	is_global_state_init.store(false);
	is_refl_thread_processing.store(false);
	is_running.store(true);
	local_states_have_changed.store(false);
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
