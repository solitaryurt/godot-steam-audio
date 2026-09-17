#include "stream.hpp"
#include "config.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "server.hpp"
#include "steam_audio.hpp"
#include <phonon.h>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>

SteamAudioStream::SteamAudioStream() {}
SteamAudioStream::~SteamAudioStream() {}

void SteamAudioStream::_bind_methods() {}

Ref<AudioStreamPlayback> SteamAudioStream::_instantiate_playback() const {
	Ref<SteamAudioStreamPlayback> playback;
	playback.instantiate();
	playback->set_stream(stream);
	playback->parent = parent;

	return playback;
}

void SteamAudioStream::set_stream(Ref<AudioStream> p_stream) { stream = p_stream; }
Ref<AudioStream> SteamAudioStream::get_stream() { return this->stream; }

// ----------------------------------------------------
// SteamAudioStreamPlayback

SteamAudioStreamPlayback::SteamAudioStreamPlayback() {}
SteamAudioStreamPlayback::~SteamAudioStreamPlayback() {}

static int32_t write_silence(AudioFrame *buffer, int32_t frames) {
	for (int i = 0; i < frames; i++) {
		buffer[i].left = 0.0f;
		buffer[i].right = 0.0f;
	}
	return frames;
}

static void set_buf_samples(LocalSteamAudioState *ls, int n) {
	ls->bufs.in.numSamples = n;
	ls->bufs.direct.numSamples = n;
	ls->bufs.mono.numSamples = n;
	ls->bufs.ambi.numSamples = n;
	ls->bufs.out.numSamples = n;
	ls->bufs.refl_ambi.numSamples = n;
	ls->bufs.refl_out.numSamples = n;
	ls->bufs.path_out.numSamples = n;
}

static void scale_audio_buffer(IPLAudioBuffer &buffer, float gain) {
	if (gain == 1.0f) {
		return;
	}
	for (int c = 0; c < buffer.numChannels; c++) {
		for (int i = 0; i < buffer.numSamples; i++) {
			buffer.data[c][i] *= gain;
		}
	}
}

int32_t SteamAudioStreamPlayback::_mix(AudioFrame *buffer, float rate_scale, int32_t frames) {
	if (parent == nullptr || stream_playback.is_null() || Engine::get_singleton()->is_editor_hint()) {
		return write_silence(buffer, frames);
	}

	auto gs = SteamAudioServer::get_singleton()->get_global_state(false);
	if (gs == nullptr) {
		return write_silence(buffer, frames);
	}

	SteamAudio::log(SteamAudio::log_debug, "mixing");

	LocalSteamAudioState *ls = parent->get_local_state();
	if (ls == nullptr) {
		return write_silence(buffer, frames);
	}
	std::unique_lock lock(ls->mux);

	if (parent == nullptr) {
		return write_silence(buffer, frames);
	}
	ls = parent->get_local_state();
	if (ls == nullptr || !ls->src.player) {
		return write_silence(buffer, frames);
	}
	if (!is_active.load()) {
		write_silence(buffer, frames);
		return 0;
	}
	if (needs_effect_reset.exchange(false)) {
		iplReflectionEffectReset(ls->fx.refl);
		iplAmbisonicsDecodeEffectReset(ls->fx.refl_dec);
		if (ls->fx.path) {
			iplPathEffectReset(ls->fx.path);
		}
		reflection_tail_remaining.store(false);
		reflection_decode_tail_remaining.store(false);
		path_tail_remaining.store(false);
	}

	PackedVector2Array mixed_frames = stream_playback->mix_audio(rate_scale, frames);
	int32_t total = int(mixed_frames.size());
	bool input_ended = total < frames || !stream_playback->is_playing();
	int32_t frame_size = gs->audio_cfg.frameSize;
	if (frame_size <= 0) {
		return write_silence(buffer, frames);
	}

	int32_t input_offset = 0;
	int32_t written = 0;
	bool processed_wet_effect = false;
	while (input_offset < total) {
		int32_t valid_input = total - input_offset;
		if (valid_input > frame_size) {
			valid_input = frame_size;
		}
		int32_t chunk = frame_size;
		set_buf_samples(ls, chunk);

		for (int i = 0; i < chunk; i++) {
			if (i < valid_input) {
				ls->bufs.in.data[0][i] = mixed_frames[input_offset + i].x;
				ls->bufs.in.data[1][i] = mixed_frames[input_offset + i].y;
			} else {
				ls->bufs.in.data[0][i] = 0.0f;
				ls->bufs.in.data[1][i] = 0.0f;
			}
		}

		if (ls->cfg.is_air_absorp_on) {
			ls->direct_outputs.flags = static_cast<IPLDirectEffectFlags>(
					ls->direct_outputs.flags |
					IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION);
		}

		if (ls->cfg.is_dist_attn_on) {
			ls->direct_outputs.flags = static_cast<IPLDirectEffectFlags>(
					ls->direct_outputs.flags |
					IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION);
		}
		if (ls->cfg.is_occlusion_on) {
			ls->direct_outputs.flags = static_cast<IPLDirectEffectFlags>(
					ls->direct_outputs.flags |
					IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION |
					IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
			ls->direct_outputs.transmissionType = ls->cfg.transmission_type;
		}
		if (ls->cfg.is_directivity_on) {
			ls->direct_outputs.flags = static_cast<IPLDirectEffectFlags>(
					ls->direct_outputs.flags |
					IPL_DIRECTEFFECTFLAGS_APPLYDIRECTIVITY);
		}

		if (ls->direct_outputs.flags != 0) {
			iplDirectEffectApply(
					ls->fx.direct, &ls->direct_outputs,
					&ls->bufs.in, &ls->bufs.direct);
		} else {
			for (int i = 0; i < ls->bufs.direct.numChannels; i++) {
				for (int j = 0; j < chunk; j++) {
					ls->bufs.direct.data[i][j] = 0.0f;
				}
			}

			iplAudioBufferMix(gs->ctx, &ls->bufs.in, &ls->bufs.direct);
		}

		IPLAmbisonicsDecodeEffectParams dec_params{};
		dec_params.orientation = ls->listener_coords;
		dec_params.order = ls->cfg.ambisonics_order;
		dec_params.hrtf = gs->hrtf;
		dec_params.binaural = IPL_TRUE;

		if (ls->cfg.is_ambisonics_on) {
			iplAudioBufferDownmix(gs->ctx, &ls->bufs.direct, &ls->bufs.mono);

			IPLAmbisonicsEncodeEffectParams enc_params{};
			enc_params.direction = ipl_vec3_from(ls->dir_to_listener);
			enc_params.order = ls->cfg.ambisonics_order;
			iplAmbisonicsEncodeEffectApply(
					ls->fx.enc, &enc_params,
					&ls->bufs.mono, &ls->bufs.ambi);

			iplAmbisonicsDecodeEffectApply(
					ls->fx.dec, &dec_params,
					&ls->bufs.ambi, &ls->bufs.out);
			SteamAudio::log(SteamAudio::log_debug, "mixing: finished ambisonics");
		} else {
			for (int i = 0; i < ls->bufs.out.numChannels; i++) {
				for (int j = 0; j < chunk; j++) {
					ls->bufs.out.data[i][j] = 0.0f;
				}
			}
			iplAudioBufferMix(gs->ctx, &ls->bufs.direct, &ls->bufs.out);
		}
		scale_audio_buffer(ls->bufs.out, ls->cfg.direct_mix_level);

		gs->refl_ir_lock.lock();
		if (ls->refl_outputs.ir != nullptr && ls->cfg.is_reflection_on) {
			processed_wet_effect = true;
			iplAudioBufferDownmix(gs->ctx, &ls->bufs.in, &ls->bufs.mono);
			// Listener-centric baked reverb has no source-distance falloff.
			// Godot attenuates the final mix when Steam Audio attenuation is off.
			if (ls->refl_outputs_baked && ls->cfg.is_dist_attn_on) {
				for (int i = 0; i < chunk; i++) {
					ls->bufs.mono.data[0][i] *= ls->direct_outputs.distanceAttenuation;
				}
			}
			ls->refl_outputs.numChannels = ambisonic_channels_from(ls->cfg.ambisonics_order);
			ls->refl_outputs.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
			ls->refl_outputs.irSize = int(SteamAudioConfig::max_refl_duration * float(gs->audio_cfg.samplingRate));
			IPLAudioEffectState reflection_state = iplReflectionEffectApply(
					ls->fx.refl, &ls->refl_outputs, &ls->bufs.mono, &ls->bufs.refl_ambi, nullptr);
			reflection_tail_remaining.store(reflection_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);

			IPLAudioEffectState decode_state = iplAmbisonicsDecodeEffectApply(
					ls->fx.refl_dec, &dec_params,
					&ls->bufs.refl_ambi, &ls->bufs.refl_out);
			reflection_decode_tail_remaining.store(decode_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
			scale_audio_buffer(ls->bufs.refl_out, ls->cfg.reflection_mix_level);

			SteamAudio::log(SteamAudio::log_debug, "mixing: mixing reflection and direct buffers");
			iplAudioBufferMix(gs->ctx, &ls->bufs.refl_out, &ls->bufs.out);
		}
		if (ls->fx.path && ls->path_outputs.shCoeffs) {
			processed_wet_effect = true;
			IPLPathEffectParams path_params = ls->path_outputs;
			path_params.binaural = IPL_TRUE;
			path_params.hrtf = gs->hrtf;
			path_params.listener = ls->listener_coords;
			path_params.normalizeEQ = ls->cfg.pathing_normalize_eq ? IPL_TRUE : IPL_FALSE;
			iplAudioBufferDownmix(gs->ctx, &ls->bufs.in, &ls->bufs.mono);
			IPLAudioEffectState path_state = iplPathEffectApply(
					ls->fx.path, &path_params, &ls->bufs.mono, &ls->bufs.path_out);
			path_tail_remaining.store(path_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
			scale_audio_buffer(ls->bufs.path_out, ls->cfg.pathing_mix_level);
			iplAudioBufferMix(gs->ctx, &ls->bufs.path_out, &ls->bufs.out);
		}
		gs->refl_ir_lock.unlock();

		int32_t output_count = frames - written;
		if (output_count > chunk) {
			output_count = chunk;
		}
		for (int i = 0; i < output_count; i++) {
			buffer[written + i].left = ls->bufs.out.data[0][i];
			buffer[written + i].right = ls->bufs.out.data[1][i];
		}
		input_offset += valid_input;
		written += output_count;
	}

	if (input_ended && ls->cfg.effect_tails) {
		while (written < frames) {
			bool had_reflection_tail = reflection_tail_remaining.load();
			bool had_decode_tail = reflection_decode_tail_remaining.load();
			bool had_path_tail = path_tail_remaining.load();
			if (!had_reflection_tail && !had_decode_tail && !had_path_tail) {
				break;
			}
			processed_wet_effect = true;

			int32_t chunk = frames - written;
			if (chunk > frame_size) {
				chunk = frame_size;
			}
			set_buf_samples(ls, chunk);
			for (int c = 0; c < ls->bufs.out.numChannels; c++) {
				for (int i = 0; i < chunk; i++) {
					ls->bufs.out.data[c][i] = 0.0f;
				}
			}

			gs->refl_ir_lock.lock();
			if (had_reflection_tail) {
				IPLAudioEffectState reflection_state = iplReflectionEffectGetTail(
						ls->fx.refl, &ls->bufs.refl_ambi, nullptr);
				reflection_tail_remaining.store(reflection_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
				IPLAmbisonicsDecodeEffectParams dec_params{};
				dec_params.orientation = ls->listener_coords;
				dec_params.order = ls->cfg.ambisonics_order;
				dec_params.hrtf = gs->hrtf;
				dec_params.binaural = IPL_TRUE;
				IPLAudioEffectState decode_state = iplAmbisonicsDecodeEffectApply(
						ls->fx.refl_dec, &dec_params, &ls->bufs.refl_ambi, &ls->bufs.refl_out);
				reflection_decode_tail_remaining.store(decode_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
				scale_audio_buffer(ls->bufs.refl_out, ls->cfg.reflection_mix_level);
				iplAudioBufferMix(gs->ctx, &ls->bufs.refl_out, &ls->bufs.out);
			} else if (had_decode_tail) {
				IPLAudioEffectState decode_state = iplAmbisonicsDecodeEffectGetTail(
						ls->fx.refl_dec, &ls->bufs.refl_out);
				reflection_decode_tail_remaining.store(decode_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
				scale_audio_buffer(ls->bufs.refl_out, ls->cfg.reflection_mix_level);
				iplAudioBufferMix(gs->ctx, &ls->bufs.refl_out, &ls->bufs.out);
			}
			if (had_path_tail && ls->fx.path) {
				IPLAudioEffectState path_state = iplPathEffectGetTail(ls->fx.path, &ls->bufs.path_out);
				path_tail_remaining.store(path_state == IPL_AUDIOEFFECTSTATE_TAILREMAINING);
				scale_audio_buffer(ls->bufs.path_out, ls->cfg.pathing_mix_level);
				iplAudioBufferMix(gs->ctx, &ls->bufs.path_out, &ls->bufs.out);
			}
			gs->refl_ir_lock.unlock();

			for (int i = 0; i < chunk; i++) {
				buffer[written + i].left = ls->bufs.out.data[0][i];
				buffer[written + i].right = ls->bufs.out.data[1][i];
			}
			written += chunk;
		}
	}

	if (input_ended && (!ls->cfg.effect_tails ||
			(!reflection_tail_remaining.load() && !reflection_decode_tail_remaining.load() && !path_tail_remaining.load()))) {
		is_active.store(false);
	}

	set_buf_samples(ls, frame_size);

	for (int i = written; i < frames; i++) {
		buffer[i].left = 0.0f;
		buffer[i].right = 0.0f;
	}

	SteamAudio::log(SteamAudio::log_debug, "mixing: done");
	if (input_ended && !ls->cfg.effect_tails) {
		return total;
	}
	if (input_ended && !processed_wet_effect && total < frames) {
		return total;
	}
	return input_ended ? written : frames;
}

void SteamAudioStreamPlayback::_bind_methods() {
	ClassDB::bind_method(D_METHOD("play_stream", "stream", "from_offset", "volume_db", "pitch_scale"), &SteamAudioStreamPlayback::play_stream, DEFVAL(0), DEFVAL(0), DEFVAL(1.0));
}

int SteamAudioStreamPlayback::play_stream(const Ref<AudioStream> &p_stream, float p_from_offset, float p_volume_db, float p_pitch_scale) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return 0;
	}

	stream = p_stream;
	stream_playback = stream->instantiate_playback();
	needs_effect_reset.store(true);
	is_active.store(true);
	stream_playback->start(p_from_offset);

	return 0;
}

void SteamAudioStreamPlayback::_start(double from_pos) {
	needs_effect_reset.store(true);
	if (stream_playback == nullptr) {
		if (stream != nullptr) {
			is_active.store(true);
			play_stream(stream, float(from_pos), 0.0, 1.0); // FIXME: do not assume these params
		}
		return;
	} else if (stream_playback->is_playing()) {
		return;
	}
	stream_playback->start(from_pos);
	is_active.store(true);
}

void SteamAudioStreamPlayback::_stop() {
	is_active.store(false);
	reflection_tail_remaining.store(false);
	reflection_decode_tail_remaining.store(false);
	path_tail_remaining.store(false);
	if (stream_playback == nullptr || !stream_playback->is_playing()) {
		return;
	}
	stream_playback->stop();
}

bool SteamAudioStreamPlayback::_is_playing() const { return is_active; }
void SteamAudioStreamPlayback::set_stream(Ref<AudioStream> p_stream) { stream = p_stream; }
Ref<AudioStreamPlayback> SteamAudioStreamPlayback::get_stream_playback() { return this->stream_playback; }
