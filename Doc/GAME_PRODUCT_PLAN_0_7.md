# 0.7.0 game-language product plan review

**Type:** engineering roadmap / feature plan. **Verdict:** ready to start dependency-first implementation; **not ready to release**.

## Strategy test

Keep Noqeri general-purpose, provide first-class reusable game libraries, and demonstrate a real Noqeri-compiled 3D desktop application before selling the language as a game-development tool.

The technical problem is specific, but user adoption and comparative differentiation are **not verifiable from the supplied plan**.

## ThoughtfulBits-aligned roadmap scorecard

| Area | Assessment | Evidence |
| --- | --- | --- |
| Product strategy (coherence) | Present but weak | Clear use case, no user interviews or adoption proof |
| Product and roadmap | Strong as a gated implementation plan | Explicit compiler -> FFI -> GPU -> playable-sample dependencies |
| Product performance metrics | Present but weak | First-run success, time-to-first-frame and p95 frame-time defined, no measured baselines |
| Launch and GTM | Out of scope for an engineering roadmap | To be designed once executable foundation works |

## Developer experience outcome

The first-run flow is: install a verified compiler -> select a documented game template -> build -> run -> see a rendered frame -> inspect errors and frame timing. Every broken step must expose a concrete diagnostic. No scene editor, account registration or visual design system is required for initial activation.

## Checklist and go/no-go

- [ ] Proven Stage-2 compiler and executable linking, Linux x86-64.
- [ ] Float/record/array/pointer calling convention test matrix.
- [ ] Versioned FFI including real C library and cleanup.
- [ ] Basic window/event lifecycle on a main thread.
- [ ] GPU context, shaders, buffers and first triangle.
- [ ] Textured cube controlled by Noqeri input.
- [ ] Reproducible game template and one documented command.
- [ ] Run baseline suites plus negative/error-path scenarios.
- [ ] Establish first-build success rate and real frame-time baselines.
- [ ] Run developer feedback sessions; identify top onboarding failures.
- [ ] Publish platform support only with executable proof.
- [ ] Qualify the general-purpose language via `scripts/qualify-release.mjs` and its documented compiler/runtime/distribution gates.
- [ ] Advertise 3D-game-ready support only after the additional GPU/hardware/gameplay gates pass; this is separate from general-purpose language stability.

## Explicit deferrals

Advanced ECS, PBR, ray tracing, multiplayer, animation, editor and mobile-console targets are deferred until a complete native first frame passes. No invented feature claims or performance numbers are permitted.

## Owners / acceptance

Planner maintains dependency gates and release checklist. Compiler engineer owns native execution/FFI. Gameplay engineer owns math/timing/collision tests. Graphics engineer owns first rendered frame, verified on hardware. QA owns reproducible machine evidence. Developer experience owner tests the first-run flow. One team member may perform multiple roles; each gate still needs its own evidence.

## Questions not yet answered

1. Which developer cohort will dogfood the first SDK?
2. What trusted compiler seed is available for Linux?
3. What ABI/native library is the first stable target?
4. Which graphics host machine is used for tests?
5. What does the minimal Noqeri 3D example compile to?
6. How are package versions and binary artifacts signed?
7. What constitutes an acceptable first-project failure rate?
8. Which end-to-end compiler/runtime blockers need priority patches?
