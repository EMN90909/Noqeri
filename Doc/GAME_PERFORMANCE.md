# Game performance evidence policy

There are **no verified Noqeri native graphics benchmarks** for the v0.7.0 development track.

## Repeatable benchmark protocol

Record CPU model, operating system, GPU model, driver, power mode, display refresh rate, compiler/optimization profile, source commit SHA, shader version, asset dimensions and runtime flags. Run warmups, then record frame times over multiple runs. Do not mix CPU simulation frame time with GPU-completed frame time.

Target for one 60 FPS reference workload: 16.67 ms/frame. This is a frame budget, **not** a performance claim. Report average and p95/p99 rather than extrapolating from an idle loop.

## Required measurements

- Native compiler cold/warm build time and bootstrap reproducibility.
- Simulation timestep determinism, pause/resume and max catch-up.
- Memory allocation count and peak process/GPU use.
- Vec3 arithmetic under optimized and nonoptimized builds.
- Collision and raycast throughput for diverse shapes.
- Window creation/destruction and frame presentation time.
- Shader compile, texture upload, indexed draw and resize overhead.
- Asset validation and bounded malformed input behavior.

Only compare measurements taken on stated equivalent workloads. Include failures, disqualifying errors and all raw command outputs with reports. Failing or missing GPU tests must not be counted as passing by skipping them.
