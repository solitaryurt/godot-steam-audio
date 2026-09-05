# Standalone SDK Tests

Run from the repository root with the installed Linux x64 Steam Audio SDK:

```sh
make -C tests -B check
```

`-B` rebuilds existing binaries, including any left over from sanitizer runs.

- `probe_batch_test`: exact SDK 4.5.3 probe coordinates/radii, serialization, and runtime pathing/reflection output. Exercises the server's shared neighborhood query for boundary, dense, occluded, and ambiguous influences. Four independent simulators test both layers, either layer alone, and neither layer. A wall blocks direct sound; positive pathing requires finite, nonzero directional SH. Reflections render a unit impulse, checking silent priming, nonzero response/tail, and silence without the reflection layer.
- `probe_bake_failure_test`: linker-wrapped no-output bakes must fail even when replacing an existing layer, while preserving the other layer. Successful bakes and batch operations still use the real SDK.
- `probe_bake_cancel_test`: cancellation during real SDK fresh bakes and rebakes, for both bakers, from the callback thread and another thread synchronized with the callback. Pathing must finish its progress callbacks but return failure; a linker wrapper aborts if unsafe native path cancellation is invoked. The reflection cancel wrapper forwards to the real SDK, and the test verifies cancellation stops after the first probe. Cancelled batches are only released; a fresh batch must bake successfully afterward.
- `reflection_transition_test`: public-SDK audio-value checks for the idle source-recreation/effect-reset sequence used in `server.cpp`. An empty real-time scene and baked reflective floor distinguish the modes without changing scene or transforms. Covers unread IRs in both transition directions, clearing accumulated baked energy, and clearing old input history before silent/new input reaches the destination mode. Also verifies reflection output at a probe boundary, with nine visible influences, and without dilution from an occluded unbaked batch. Checks finite output and energy bounds, not private SDK state or handle addresses. This mirrors the transition lifecycle; it does not exercise Godot's mixer locks or allocation-failure handling.

All jobs within each process are serialized, matching the SDK's process-global baker state. Cancellation must go through the core wrappers, which remember requests even when the SDK resets its internal cancellation flag before returning. Never query, save, remove layers from, or rebake a cancelled batch.

## Godot Integration

With Godot and a freshly built debug extension:

```sh
godot --headless --editor --path project --script ../tests/probe_volume_test.gd
```

The script uses a unique `user://` directory and removes its fixtures. It covers automatic filename collisions, existing file/directory protection, retained explicit/resource paths, cross-volume bake rejection, cancellation and destruction ownership cleanup, setup failures, and normal completion/rebaking. Expected logs include busy refusals and a missing-metadata error.

For visual verification, select a generated volume in a narrow Inspector, then move/rotate/scale the volume and its parent. Probe marker centers must stay at the stored world positions, while the generation outline stays world-axis-aligned and matches the generated region. Headless tests cannot verify rendered gizmos or dock sizing.

## Cancellation Behavior

Pathing uses **finish-and-discard**, not native SDK cancellation. The atomic request makes core return `false` with `pathing bake cancelled` after the SDK finishes; the completed result must not be published. The normal editor bake remains asynchronous and busy until its worker finishes, including retaining serialized bake ownership. Cancelling does not shorten that work. Exiting the scene/editor can wait for the full remaining bake while joining the worker.

This avoids SDK 4.5.3's native path cancellation race: its worker-pool cancellation predicate can wake a worker before `mJobGraph` is initialized during visibility generation. It also avoids creating a partially cancelled path layer whose `GetDataSize` can crash. Do not call `iplPathBakerCancelBake` directly. Reflection cancellation remains native: SDK 4.5.3 sets an atomic flag and stops between probes without cancelling its worker pool. Core rejects the result before inspecting any partial reflection data.

## Sanitizers And SDK Limitations

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  make -C tests -B check \
  CXXFLAGS='-O1 -g -std=c++17 -Wall -Wextra -pthread -fsanitize=address,undefined -fno-omit-frame-pointer'
```

The prebuilt SDK is not sanitizer-instrumented. Leak detection is disabled in this command because the runtime fixture reports allocations leaked inside `libphonon.so`; this is not a leak-clean validation.

Restore normal test binaries afterward with `make -C tests -B check`.
