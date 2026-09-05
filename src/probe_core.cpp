#include "probe_core.hpp"
#include <atomic>
#include <cstring>

// Like the SDK's process-global bakers, these flags require serialized jobs.
// Atomic flags also cover cancellation delivered from another thread.
static std::atomic<bool> pathing_bake_cancelled{ false };
static std::atomic<bool> reflections_bake_cancelled{ false };

static bool fail(std::string *err, const char *msg) {
	if (err) {
		*err = msg;
	}
	return false;
}

IPLMatrix4x4 probe_core_volume_matrix(float ox, float oy, float oz, float sx, float sy, float sz) {
	// SDK 4.5.3 probe generation reads column-major storage, despite the public
	// matrix's row-major documentation. Its local box is [-0.5, 0.5]^3.
	return IPLMatrix4x4{ {
			{ sx, 0.f, 0.f, 0.f },
			{ 0.f, sy, 0.f, 0.f },
			{ 0.f, 0.f, sz, 0.f },
			{ ox, oy, oz, 1.f },
	} };
}

IPLContext probe_core_create_context(std::string *err) {
	IPLContextSettings cfg{};
	cfg.version = STEAMAUDIO_VERSION;
	IPLContext ctx = nullptr;
	if (iplContextCreate(&cfg, &ctx) != IPL_STATUS_SUCCESS || ctx == nullptr) {
		fail(err, "iplContextCreate failed");
		return nullptr;
	}
	return ctx;
}

void probe_core_destroy_context(IPLContext *ctx) {
	if (ctx && *ctx) {
		iplContextRelease(ctx);
		*ctx = nullptr;
	}
}

IPLScene probe_core_create_empty_scene(IPLContext ctx, std::string *err) {
	if (ctx == nullptr) {
		fail(err, "null context");
		return nullptr;
	}
	IPLSceneSettings scene_cfg{};
	scene_cfg.type = IPL_SCENETYPE_DEFAULT;
	IPLScene scene = nullptr;
	if (iplSceneCreate(ctx, &scene_cfg, &scene) != IPL_STATUS_SUCCESS || scene == nullptr) {
		fail(err, "iplSceneCreate failed");
		return nullptr;
	}
	iplSceneCommit(scene);
	return scene;
}

bool probe_core_add_box(IPLScene scene, float cx, float cy, float cz, float hx, float hy, float hz, std::string *err) {
	if (scene == nullptr) {
		return fail(err, "null scene");
	}

	IPLVector3 verts[8] = {
		{ cx - hx, cy - hy, cz - hz },
		{ cx + hx, cy - hy, cz - hz },
		{ cx + hx, cy - hy, cz + hz },
		{ cx - hx, cy - hy, cz + hz },
		{ cx - hx, cy + hy, cz - hz },
		{ cx + hx, cy + hy, cz - hz },
		{ cx + hx, cy + hy, cz + hz },
		{ cx - hx, cy + hy, cz + hz },
	};
	IPLTriangle tris[12] = {
		{ { 0, 2, 1 } },
		{ { 0, 3, 2 } },
		{ { 4, 5, 6 } },
		{ { 4, 6, 7 } },
		{ { 0, 1, 5 } },
		{ { 0, 5, 4 } },
		{ { 3, 7, 6 } },
		{ { 3, 6, 2 } },
		{ { 0, 4, 7 } },
		{ { 0, 7, 3 } },
		{ { 1, 2, 6 } },
		{ { 1, 6, 5 } },
	};
	IPLint32 mat_indices[12] = {};
	IPLMaterial mat{ { 0.1f, 0.2f, 0.3f }, 0.05f, { 0.1f, 0.05f, 0.1f } };

	IPLStaticMeshSettings mesh_cfg{};
	mesh_cfg.numVertices = 8;
	mesh_cfg.numTriangles = 12;
	mesh_cfg.numMaterials = 1;
	mesh_cfg.vertices = verts;
	mesh_cfg.triangles = tris;
	mesh_cfg.materialIndices = mat_indices;
	mesh_cfg.materials = &mat;

	IPLStaticMesh mesh = nullptr;
	if (iplStaticMeshCreate(scene, &mesh_cfg, &mesh) != IPL_STATUS_SUCCESS || mesh == nullptr) {
		return fail(err, "iplStaticMeshCreate failed");
	}
	iplStaticMeshAdd(mesh, scene);
	iplSceneCommit(scene);
	iplStaticMeshRelease(&mesh);
	return true;
}

IPLScene probe_core_create_box_scene(IPLContext ctx, float hx, float hy, float hz, std::string *err) {
	IPLScene scene = probe_core_create_empty_scene(ctx, err);
	if (scene == nullptr) {
		return nullptr;
	}
	if (!probe_core_add_box(scene, 0.f, 0.f, 0.f, hx, hy, hz, err)) {
		iplSceneRelease(&scene);
		return nullptr;
	}
	return scene;
}

void probe_core_destroy_scene(IPLScene *scene) {
	if (scene && *scene) {
		iplSceneRelease(scene);
		*scene = nullptr;
	}
}

bool probe_core_generate_batch(IPLContext ctx, IPLScene scene, IPLProbeGenerationType type,
		IPLMatrix4x4 volume, float spacing, float height,
		IPLProbeBatch *out_batch, int *out_count, std::string *err,
		std::vector<IPLVector3> *out_positions, std::vector<float> *out_radii) {
	if (ctx == nullptr || scene == nullptr || out_batch == nullptr) {
		return fail(err, "null argument to probe_core_generate_batch");
	}

	IPLProbeArray arr = nullptr;
	if (iplProbeArrayCreate(ctx, &arr) != IPL_STATUS_SUCCESS || arr == nullptr) {
		return fail(err, "iplProbeArrayCreate failed");
	}

	IPLProbeGenerationParams gen{};
	gen.type = type;
	gen.spacing = spacing;
	gen.height = height;
	gen.transform = volume;
	iplProbeArrayGenerateProbes(arr, scene, &gen);

	int n = iplProbeArrayGetNumProbes(arr);
	if (out_count) {
		*out_count = n;
	}
	if (n <= 0) {
		iplProbeArrayRelease(&arr);
		return fail(err, "probe generation produced 0 probes");
	}
	if (out_positions || out_radii) {
		if (out_positions) {
			out_positions->resize(size_t(n));
		}
		if (out_radii) {
			out_radii->resize(size_t(n));
		}
		for (int i = 0; i < n; i++) {
			IPLSphere probe = iplProbeArrayGetProbe(arr, i);
			if (out_positions) {
				(*out_positions)[size_t(i)] = probe.center;
			}
			if (out_radii) {
				(*out_radii)[size_t(i)] = probe.radius;
			}
		}
	}

	IPLProbeBatch batch = nullptr;
	if (iplProbeBatchCreate(ctx, &batch) != IPL_STATUS_SUCCESS || batch == nullptr) {
		iplProbeArrayRelease(&arr);
		return fail(err, "iplProbeBatchCreate failed");
	}
	iplProbeBatchAddProbeArray(batch, arr);
	iplProbeBatchCommit(batch);
	iplProbeArrayRelease(&arr);

	*out_batch = batch;
	return true;
}

bool probe_core_save_batch(IPLContext ctx, IPLProbeBatch batch, std::vector<uint8_t> *out, std::string *err) {
	if (ctx == nullptr || batch == nullptr || out == nullptr) {
		return fail(err, "null argument to probe_core_save_batch");
	}
	IPLSerializedObjectSettings so_cfg{};
	IPLSerializedObject so = nullptr;
	if (iplSerializedObjectCreate(ctx, &so_cfg, &so) != IPL_STATUS_SUCCESS || so == nullptr) {
		return fail(err, "iplSerializedObjectCreate failed");
	}
	iplProbeBatchSave(batch, so);
	IPLsize size = iplSerializedObjectGetSize(so);
	IPLbyte *data = iplSerializedObjectGetData(so);
	if (size == 0 || data == nullptr) {
		iplSerializedObjectRelease(&so);
		return fail(err, "serialized probe batch is empty");
	}
	out->assign(data, data + size);
	iplSerializedObjectRelease(&so);
	return true;
}

bool probe_core_load_batch(IPLContext ctx, const uint8_t *data, size_t size,
		IPLProbeBatch *out_batch, int *out_count, std::string *err) {
	if (ctx == nullptr || data == nullptr || size == 0 || out_batch == nullptr) {
		return fail(err, "null argument to probe_core_load_batch");
	}
	IPLSerializedObjectSettings so_cfg{};
	so_cfg.data = const_cast<IPLbyte *>(reinterpret_cast<const IPLbyte *>(data));
	so_cfg.size = size;
	IPLSerializedObject so = nullptr;
	if (iplSerializedObjectCreate(ctx, &so_cfg, &so) != IPL_STATUS_SUCCESS || so == nullptr) {
		return fail(err, "iplSerializedObjectCreate (load) failed");
	}
	IPLProbeBatch batch = nullptr;
	IPLerror load_err = iplProbeBatchLoad(ctx, so, &batch);
	iplSerializedObjectRelease(&so);
	if (load_err != IPL_STATUS_SUCCESS || batch == nullptr) {
		return fail(err, "iplProbeBatchLoad failed");
	}
	// Load does not rebuild the probe tree; RunPathing uses getInfluencingProbes
	// which dereferences it. Unity always Commit()s after load.
	iplProbeBatchCommit(batch);
	if (out_count) {
		*out_count = iplProbeBatchGetNumProbes(batch);
	}
	*out_batch = batch;
	return true;
}

bool probe_core_bake_pathing(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_samples, float radius, float threshold, float vis_range, float path_range,
		int num_threads, std::string *err, IPLProgressCallback progress_cb, void *progress_user) {
	if (ctx == nullptr || scene == nullptr || batch == nullptr) {
		return fail(err, "null argument to probe_core_bake_pathing");
	}
	IPLBakedDataIdentifier id{};
	id.type = IPL_BAKEDDATATYPE_PATHING;
	id.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;

	IPLPathBakeParams params{};
	params.scene = scene;
	params.probeBatch = batch;
	params.identifier = id;
	params.numSamples = num_samples > 0 ? num_samples : 1;
	params.radius = radius > 0.f ? radius : 1.f;
	params.threshold = threshold;
	params.visRange = vis_range;
	params.pathRange = path_range;
	params.numThreads = num_threads > 0 ? num_threads : 1;

	// Only the newly baked layer may establish success. The caller owns this
	// job-local batch; leave other baked layers untouched.
	iplProbeBatchRemoveData(batch, &id);
	pathing_bake_cancelled.store(false);
	iplPathBakerBake(ctx, &params, progress_cb, progress_user);
	// Pathing cancellation finishes the SDK bake, then discards its result.
	// The caller must release this job-local batch without publishing it.
	if (pathing_bake_cancelled.load()) {
		return fail(err, "pathing bake cancelled");
	}
	if (iplProbeBatchGetDataSize(batch, &id) == 0) {
		return fail(err, "pathing bake produced no data");
	}
	return true;
}

void probe_core_cancel_pathing_bake(IPLContext ctx) {
	if (ctx) {
		// SDK 4.5.3 native cancellation can wake workers before their job graph
		// exists. Finish-and-discard avoids that race and unsafe partial layers.
		pathing_bake_cancelled.store(true);
	}
}

bool probe_core_bake_reflections(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_rays, int num_diffuse_samples, int num_bounces,
		float simulated_duration, float saved_duration, int order, int num_threads,
		float irradiance_min_distance, int bake_batch_size, std::string *err,
		IPLProgressCallback progress_cb, void *progress_user) {
	if (ctx == nullptr || scene == nullptr || batch == nullptr) {
		return fail(err, "null argument to probe_core_bake_reflections");
	}
	IPLBakedDataIdentifier id{};
	id.type = IPL_BAKEDDATATYPE_REFLECTIONS;
	id.variation = IPL_BAKEDDATAVARIATION_REVERB;

	IPLReflectionsBakeParams params{};
	params.scene = scene;
	params.probeBatch = batch;
	params.sceneType = IPL_SCENETYPE_DEFAULT;
	params.identifier = id;
	params.bakeFlags = IPL_REFLECTIONSBAKEFLAGS_BAKECONVOLUTION;
	params.numRays = num_rays > 0 ? num_rays : 1;
	params.numDiffuseSamples = num_diffuse_samples > 0 ? num_diffuse_samples : 1;
	params.numBounces = num_bounces > 0 ? num_bounces : 1;
	params.simulatedDuration = simulated_duration > 0.f ? simulated_duration : 0.1f;
	params.savedDuration = saved_duration > 0.f ? saved_duration : params.simulatedDuration;
	if (params.savedDuration > params.simulatedDuration) {
		params.savedDuration = params.simulatedDuration;
	}
	params.order = order >= 0 ? order : 0;
	params.numThreads = num_threads > 0 ? num_threads : 1;
	params.rayBatchSize = 1;
	params.irradianceMinDistance = irradiance_min_distance > 0.f ? irradiance_min_distance : 1.f;
	params.bakeBatchSize = bake_batch_size > 0 ? bake_batch_size : 1;

	// Reflection rebakes otherwise reuse the old layer, even if no new data is produced.
	iplProbeBatchRemoveData(batch, &id);
	reflections_bake_cancelled.store(false);
	iplReflectionsBakerBake(ctx, &params, progress_cb, progress_user);
	if (reflections_bake_cancelled.load()) {
		return fail(err, "reflection bake cancelled");
	}
	if (iplProbeBatchGetDataSize(batch, &id) == 0) {
		return fail(err, "reflection bake produced no data");
	}
	return true;
}

void probe_core_cancel_reflections_bake(IPLContext ctx) {
	if (ctx) {
		reflections_bake_cancelled.store(true);
		// Unlike pathing, SDK 4.5.3 only sets an atomic flag here; it stops
		// between probes without cancelling the worker pool.
		iplReflectionsBakerCancelBake(ctx);
	}
}

int probe_core_batch_num_probes(IPLProbeBatch batch) {
	if (batch == nullptr) {
		return 0;
	}
	return iplProbeBatchGetNumProbes(batch);
}

IPLsize probe_core_pathing_data_size(IPLProbeBatch batch) {
	if (batch == nullptr) {
		return 0;
	}
	IPLBakedDataIdentifier id{};
	id.type = IPL_BAKEDDATATYPE_PATHING;
	id.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
	return iplProbeBatchGetDataSize(batch, &id);
}

IPLsize probe_core_reflections_data_size(IPLProbeBatch batch) {
	if (batch == nullptr) {
		return 0;
	}
	IPLBakedDataIdentifier id{};
	id.type = IPL_BAKEDDATATYPE_REFLECTIONS;
	id.variation = IPL_BAKEDDATAVARIATION_REVERB;
	return iplProbeBatchGetDataSize(batch, &id);
}

void probe_core_release_batch(IPLProbeBatch *batch) {
	if (batch && *batch) {
		iplProbeBatchRelease(batch);
		*batch = nullptr;
	}
}
