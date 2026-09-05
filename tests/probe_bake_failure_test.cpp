#include "probe_core.hpp"
#include <cmath>
#include <cstdio>

// Linker wrapping leaves the real SDK responsible for batch data while letting
// us reproduce its void bake APIs returning without producing a layer.
static bool skip_bake = false;
extern "C" {
void IPLCALL __real_iplPathBakerBake(IPLContext, IPLPathBakeParams *, IPLProgressCallback, void *);
void IPLCALL __real_iplReflectionsBakerBake(IPLContext, IPLReflectionsBakeParams *, IPLProgressCallback, void *);

void IPLCALL __wrap_iplPathBakerBake(IPLContext ctx, IPLPathBakeParams *params,
		IPLProgressCallback callback, void *user) {
	if (!skip_bake) {
		__real_iplPathBakerBake(ctx, params, callback, user);
	}
}

void IPLCALL __wrap_iplReflectionsBakerBake(IPLContext ctx, IPLReflectionsBakeParams *params,
		IPLProgressCallback callback, void *user) {
	if (!skip_bake) {
		__real_iplReflectionsBakerBake(ctx, params, callback, user);
	}
}
}

static int failures = 0;
static bool valid_progress = true;
static void expect(bool condition, const char *message) {
	std::printf("%s: %s\n", condition ? "ok" : "FAIL", message);
	failures += !condition;
}

static void IPLCALL progress(float fraction, void *user) {
	valid_progress = valid_progress && std::isfinite(fraction) && fraction >= 0.f && fraction <= 1.f;
	++*static_cast<int *>(user);
}

int main() {
	std::string err;
	IPLContext ctx = probe_core_create_context(&err);
	IPLScene scene = probe_core_create_box_scene(ctx, 8.f, 0.25f, 8.f, &err);
	IPLProbeBatch batch = nullptr;
	bool generated = scene && probe_core_generate_batch(ctx, scene, IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
			probe_core_volume_matrix(0.f, 2.f, 0.f, 8.f, 4.f, 8.f), 4.f, 1.5f,
			&batch, nullptr, &err);
	expect(generated, "create bake failure fixture");
	if (!generated) {
		std::fprintf(stderr, "%s\n", err.c_str());
		probe_core_destroy_scene(&scene);
		probe_core_destroy_context(&ctx);
		return 1;
	}

	int progress_calls = 0;
	auto bake_path = [&]() {
		err.clear();
		return probe_core_bake_pathing(ctx, scene, batch, 1, 1.f, 0.5f, 20.f, 40.f, 1,
				&err, progress, &progress_calls);
	};
	auto bake_reflections = [&]() {
		err.clear();
		return probe_core_bake_reflections(ctx, scene, batch, 32, 8, 2, 0.1f, 0.1f, 1, 1, 1.f, 1,
				&err, progress, &progress_calls);
	};

	skip_bake = true;
	expect(!bake_path() && err == "pathing bake produced no data", "no-output path bake fails with an error");
	expect(!bake_reflections() && err == "reflection bake produced no data", "no-output reflection bake fails with an error");
	expect(progress_calls == 0, "no-output bakers did not invoke progress");

	skip_bake = false;
	expect(bake_path() && probe_core_pathing_data_size(batch) > 0, "real path bake succeeds");
	expect(progress_calls > 0, "path callback and user data reach SDK");
	progress_calls = 0;
	expect(bake_reflections() && probe_core_reflections_data_size(batch) > 0, "real reflection bake succeeds");
	expect(progress_calls > 0, "reflection callback and user data reach SDK");
	IPLsize reflection_size = probe_core_reflections_data_size(batch);

	skip_bake = true;
	expect(!bake_path() && err == "pathing bake produced no data", "old path layer cannot mask failed rebake");
	expect(probe_core_pathing_data_size(batch) == 0, "failed path rebake leaves no stale layer");
	expect(probe_core_reflections_data_size(batch) == reflection_size, "failed path rebake preserves reflection layer");

	skip_bake = false;
	expect(bake_path(), "restore path layer");
	IPLsize path_size = probe_core_pathing_data_size(batch);
	skip_bake = true;
	expect(!bake_reflections() && err == "reflection bake produced no data", "old reflection layer cannot mask failed rebake");
	expect(probe_core_reflections_data_size(batch) == 0, "failed reflection rebake leaves no stale layer");
	expect(probe_core_pathing_data_size(batch) == path_size, "failed reflection rebake preserves path layer");
	expect(valid_progress, "all bake progress values are finite and within [0,1]");

	probe_core_release_batch(&batch);
	probe_core_destroy_scene(&scene);
	probe_core_destroy_context(&ctx);
	return failures ? 1 : 0;
}
