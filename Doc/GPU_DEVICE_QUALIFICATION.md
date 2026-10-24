# Physical GPU-device qualification

The `.github/workflows/gpu-physical-qualification.yml` workflow is
**operator-gated** (`workflow_dispatch`) and requires the
`self-hosted` + `physical-gpu` runner labels. It will never run
on ordinary GitHub-hosted CI to create spurious hardware evidence.

## Runner prerequisites

1. Independently establish that a genuine GPU and its proprietary or
   open-source kernel/user driver stack are installed and accessible.
   The workflow checks the Linux DRM render node and a physical
   desktop session. It does **not** initialize PCI hardware itself.
2. Install cmake, C++17, Node.js, npm, Python, a real Chrome/Chromium
   binary and its GPU drivers; ensure Playwright can launch the
   browser on an attached desktop/X11/Wayland display.
3. In GitHub Actions select **Physical GPU shader qualification**,
   start the workflow manually and select Intel, NVIDIA or AMD,
   matching the operator-confirmed adapter.
4. The workflow compiles Noqeri source to WGSL using the actual
   compiler. It runs a real-browser WebGPU test (without SwiftShader
   or other software GPU fallback flags), with:
   - u32 compute shader output readback and exact expected results
   - f32 compute shader output readback and approximate expected results
   - Vec4 compute buffer readback and finite-value checks
   - RGBA8 texture upload and fragment pipeline submission
   - device destruction and `device.lost` lifecycle test
5. Retain the workflow logs, JSON evidence and browser screenshot
   from the artifact `noqeri-physical-gpu-evidence-<run-id>`.
   The evidence includes adapter information and the test revision.

The harness rejects a missing GPU, fallback adapter, known software
renderer names, unmatched selected vendor and device-test failures.
Because WebGPU adapter names and user-generated JSON can be spoofed,
**this is a test protocol, not an independent cryptographic
attestation.** A trusted runner inventory and operator inspection
must corroborate hardware identity. Driver versions, renderer
backend, GPU model, OS, device loss and timeouts should be retained.

## Qualification status

An artifact produced by a real hardware job with accepted identity,
expected readbacks, a successful fragment pipeline, verified logs
and operator corroboration can support a **device-specific**
qualification, not universal support across GPU vendors.

**No physical device was available in the regular hosted CI used
for this implementation.** Software/emulated validation is not
physical GPU qualification; the hardware acceptance gate therefore
remains unqualified until this manual test completes on hardware.
