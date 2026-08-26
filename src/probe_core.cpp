#include "probe_core.hpp"
#include <cstring>

static bool fail(std::string *err, const char *msg) {
	if (err) {
		*err = msg;
	}
	return false;
}

IPLMatrix4x4 probe_core_volume_matrix(float ox, float oy, float oz, float sx, float sy, float sz) {
	// Maps (0,0,0) -> origin - size/2, (1,1,1) -> origin + size/2.
	return IPLMatrix4x4{ {
			{ sx, 0.f, 0.f, ox - 0.5f * sx },
			{ 0.f, sy, 0.f, oy - 0.5f * sy },
			{ 0.f, 0.f, sz, oz - 0.5f * sz },
			{ 0.f, 0.f, 0.f, 1.f },
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
		IPLProbeBatch *out_batch, int *out_count, std::string *err) {
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
	if (out_count) {
		*out_count = iplProbeBatchGetNumProbes(batch);
	}
	*out_batch = batch;
	return true;
}

bool probe_core_bake_pathing(IPLContext ctx, IPLScene scene, IPLProbeBatch batch,
		int num_samples, float radius, float threshold, float vis_range, float path_range,
		int num_threads, std::string *err) {
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

	iplPathBakerBake(ctx, &params, nullptr, nullptr);
	return true;
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

void probe_core_release_batch(IPLProbeBatch *batch) {
	if (batch && *batch) {
		iplProbeBatchRelease(batch);
		*batch = nullptr;
	}
}
