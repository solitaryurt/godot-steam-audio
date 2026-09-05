#include "probe_core.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

static int failures = 0;
static std::atomic<int> native_reflection_cancels{ 0 };

extern "C" {
void IPLCALL __wrap_iplPathBakerCancelBake(IPLContext) {
	// Fail deterministically instead of entering the SDK's intermittent crash.
	std::fprintf(stderr, "FAIL: unsafe native path cancellation invoked\n");
	std::abort();
}

void IPLCALL __real_iplReflectionsBakerCancelBake(IPLContext);
void IPLCALL __wrap_iplReflectionsBakerCancelBake(IPLContext ctx) {
	++native_reflection_cancels;
	__real_iplReflectionsBakerCancelBake(ctx);
}
}

static void expect(bool condition, const char *message) {
	std::printf("%s: %s\n", condition ? "ok" : "FAIL", message);
	failures += !condition;
}

struct Cancellation {
	IPLContext ctx;
	bool reflections;
	bool other_thread;
	int calls = 0;
	float last_progress = -1.f;
};

static void IPLCALL cancel_progress(float progress, void *user) {
	auto &cancel = *static_cast<Cancellation *>(user);
	cancel.last_progress = progress;
	if (cancel.calls++ != 0) {
		return;
	}
	auto request = [&]() {
		if (cancel.reflections) {
			probe_core_cancel_reflections_bake(cancel.ctx);
		} else {
			probe_core_cancel_pathing_bake(cancel.ctx);
		}
	};
	// The callback keeps the bake alive until cancellation has been delivered.
	if (cancel.other_thread) {
		std::thread worker(request);
		worker.join();
	} else {
		request();
	}
}

int main() {
	// Keep the failing SDK case visible even if a native worker crashes.
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::string err;
	IPLContext ctx = probe_core_create_context(&err);
	IPLScene scene = probe_core_create_box_scene(ctx, 8.f, 0.25f, 8.f, &err);
	IPLProbeBatch batch = nullptr;
	bool generated = scene && probe_core_generate_batch(ctx, scene, IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
			probe_core_volume_matrix(0.f, 2.f, 0.f, 8.f, 4.f, 8.f), 4.f, 1.5f,
			&batch, nullptr, &err);
	expect(generated, "create actual SDK cancellation fixture");
	if (!generated) {
		std::fprintf(stderr, "%s\n", err.c_str());
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return 1;
	}
	auto bake = [&](bool reflections, IPLProgressCallback callback = nullptr, void *user = nullptr) {
		err.clear();
		return reflections ? probe_core_bake_reflections(ctx, scene, batch,
				32, 8, 2, 0.1f, 0.1f, 1, 2, 1.f, 1, &err, callback, user) :
				probe_core_bake_pathing(ctx, scene, batch, 1, 1.f, 0.5f, 20.f, 40.f, 2, &err, callback, user);
	};
	std::vector<uint8_t> unbaked, baked;
	expect(probe_core_save_batch(ctx, batch, &unbaked, &err), "save unbaked cancellation fixture");
	expect(bake(false) && bake(true), "bake both fixture layers");
	expect(probe_core_save_batch(ctx, batch, &baked, &err), "save baked cancellation fixture");
	probe_core_release_batch(&batch);

	for (bool rebake : { false, true }) {
		for (bool reflections : { false, true }) {
			for (bool other_thread : { false, true }) {
				std::printf("  cancellation: reflections=%d rebake=%d other_thread=%d\n",
						reflections, rebake, other_thread);
				const auto &bytes = rebake ? baked : unbaked;
				bool loaded = probe_core_load_batch(ctx, bytes.data(), bytes.size(), &batch, nullptr, &err);
				expect(loaded, "load independent cancellation batch");
				if (!loaded) {
					continue;
				}
				Cancellation cancel{ ctx, reflections, other_thread };
				native_reflection_cancels.store(0);
				bool ok = bake(reflections, cancel_progress, &cancel);
				expect(cancel.calls > 0, "real SDK invoked cancellation callback");
				expect(!ok && err == (reflections ? "reflection bake cancelled" : "pathing bake cancelled"),
						"core rejects cancelled bake results");
				if (reflections) {
					expect(native_reflection_cancels.load() == 1, "reflection cancellation reaches real SDK");
					expect(cancel.calls == 1 && cancel.last_progress > 0.f && cancel.last_progress < 1.f,
							"native reflection cancellation stops after the first probe");
				} else {
					expect(native_reflection_cancels.load() == 0, "path cancellation does not cancel reflections");
					expect(cancel.calls > 1 && cancel.last_progress == 1.f,
							"path bake finishes without invoking unsafe native cancellation");
				}
				// Exercise the same release-only cleanup as the volume worker.
				probe_core_release_batch(&batch);
				expect(batch == nullptr, "cancelled batch releases safely");

				loaded = probe_core_load_batch(ctx, bytes.data(), bytes.size(), &batch, nullptr, &err);
				expect(loaded && bake(reflections), "next bake on a fresh batch resets cancellation");
				if (loaded) {
					expect((reflections ? probe_core_reflections_data_size(batch) : probe_core_pathing_data_size(batch)) > 0,
							"replacement bake has data");
				}
				probe_core_release_batch(&batch);
			}
		}
	}
	probe_core_destroy_scene(&scene);
	probe_core_destroy_context(&ctx);
	return failures ? 1 : 0;
}
