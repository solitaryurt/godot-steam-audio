#include "probe_core.hpp"
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
	bool gen_ok = probe_core_generate_batch(ctx, empty, IPL_PROBEGENERATIONTYPE_CENTROID,
			volume, 2.f, 1.5f, &centroid, &centroid_n, &err);
	expect(gen_ok, "generate centroid batch");
	expect(centroid_n == 1, "centroid batch has 1 probe");
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

	// Uniform floor over a solid box.
	IPLScene box = probe_core_create_box_scene(ctx, 8.f, 0.25f, 8.f, &err);
	expect(box != nullptr, "create box scene");
	IPLProbeBatch floor = nullptr;
	int floor_n = 0;
	IPLMatrix4x4 floor_vol = probe_core_volume_matrix(0.f, 2.f, 0.f, 16.f, 4.f, 16.f);
	bool floor_ok = box && probe_core_generate_batch(ctx, box, IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR,
			floor_vol, 4.f, 1.5f, &floor, &floor_n, &err);
	expect(floor_ok, "generate uniform-floor batch");
	expect(floor_n >= 2, "uniform-floor produced multiple probes");
	if (!floor_ok) {
		std::fprintf(stderr, "  %s\n", err.c_str());
	} else {
		std::printf("  uniform-floor probe count = %d\n", floor_n);
	}

	// Step 2: bake PATHING / DYNAMIC into the floor batch.
	bool bake_ok = floor_ok && probe_core_bake_pathing(ctx, box, floor, 1, 1.f, 0.5f, 20.f, 40.f, 2, &err);
	expect(bake_ok, "bake pathing");
	IPLsize path_bytes = floor_ok ? probe_core_pathing_data_size(floor) : 0;
	expect(path_bytes > 0, "baked pathing layer is non-empty");
	std::printf("  pathing data size = %zu\n", (size_t)path_bytes);

	std::vector<uint8_t> baked_bytes;
	bool baked_save = bake_ok && probe_core_save_batch(ctx, floor, &baked_bytes, &err);
	expect(baked_save && baked_bytes.size() >= bytes.size(), "serialize baked batch");

	IPLProbeBatch baked_loaded = nullptr;
	int baked_n = 0;
	bool baked_load = baked_save && probe_core_load_batch(ctx, baked_bytes.data(), baked_bytes.size(),
			&baked_loaded, &baked_n, &err);
	expect(baked_load && baked_n == floor_n, "reload baked batch");
	expect(baked_load && probe_core_pathing_data_size(baked_loaded) > 0, "reloaded batch still has pathing layer");

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
