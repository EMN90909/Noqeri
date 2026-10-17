# Game SDK API (source-present, unqualified)

All examples use the documented `float` type and explicit, borrowed output pointers. Record constructors and implicit heap allocations are not assumed. Modules are resolved by relative file imports. The actual public API remains experimental until compiled fixtures pass.

## Vector math

`Lib/game/math/vec3.nqr` defines `Vec3 { x: float, y: float, z: float }`.

- `vec3Set(out, x, y, z)`, `vec3Zero(out)`
- `vec3Add(out, a, b)`, `vec3Subtract(out, a, b)`, `vec3Scale(out, a, scale)`
- `vec3Dot(a, b)`, `vec3Cross(out, a, b)`
- `vec3LengthSquared(a)`, `vec3Length(a)`, `vec3DistanceSquared(a,b)`
- `vec3Normalize(out, a, epsilon)` returns false and preserves out on degenerate input
- `vec3Lerp(out, a, b, t)` does not implicitly clamp t

Inputs and output may alias for binary vector operations; intermediate scalar values are computed before any output write. All pointers must point to valid aligned storage. Floating-point overflow, NaN and infinity behavior remains subject to compiler conformance.

## Fixed-step planning

`Lib/game/core/fixed_step.nqr` defines `FixedStep` and `FramePlan`.

- `fixedStepInit(clock, stepNs, maxFrameNs, maxSteps)`
- `fixedStepValid(clock)`: rejects zero step, zero max steps, invalid accumulator and max frame greater than one second
- `fixedStepAdvance(clock, elapsedNs, plan)`: count updates and return interpolation alpha in [0,1)
- `fixedStepPause(clock, pause)`, `fixedStepReset(clock)`

A host supplies elapsed nanoseconds from a monotonic clock. The caller performs exactly `plan.steps` physics updates. `droppedInputNs` and `droppedCatchupNs` report separately discarded time, avoiding ambiguous overflow-prone sums. Paused frames do not accumulate elapsed time.

## Collision

`Lib/game/physics/aabb.nqr` exposes `aabbValid`, `aabbContainsPoint`, `aabbOverlaps`, and `aabbRayHit`. The ray direction must be nonzero. Ray maxDistance is a parameter along that direction; normalize direction for world-unit distances.

## Input transition

`Lib/game/input/button.nqr` exposes `buttonInit`, `buttonBeginFrame`, `buttonSetDown`, `buttonIsDown`, `buttonWasPressed` and `buttonWasReleased`. Call `buttonBeginFrame` once per frame, then feed host events.

## Asset boundary

`Tools/game/validate-obj.mjs` parses and validates a deliberately restricted, position-only OBJ subset, returning vertices, triangles and bounds. It does **not** load GPU buffers, materials, texture coordinates, normals, skeletons or glTF files.

## Not implemented

Matrix/quaternion operations, GPU APIs, real-time input host, audio, animations, scene/ECS, asset streaming and a playable 3D demo remain release blockers, not advertised API endpoints.
