# Physical GPU verification of Noqeri's optional AI/ML framework

Noqeri is a **general-purpose programming language**. The OpenCL backend is
an optional part of `libnoqeri_ai`, not part of the language compiler or syntax.

## What is verified, and what isn't

- The standard Ubuntu OpenCL CI builds the real `NQAI_HAVE_OPENCL=1`
  implementation. That job is **compiler coverage only**, not proof that any
  GPU kernels executed.
- `noqeri-ai-gpu-hardware-test` **requires** an OpenCL
  `CL_DEVICE_TYPE_GPU`. There is no CPU fallback or test skip.
- On that device the test executes five GPU matrix shape cases, CPU/GPU forward
  and backward gradient parity, four AdamW optimizer updates of a two-block
  single-head transformer, and inference evaluation. It rejects nonfinite
  results and discrepancies outside defined error tolerances.
- The workflow saves device vendor/name/driver, OpenCL platform inventory,
  selected device type, numerical evidence and CLI training logs.
- OpenCL's reported GPU type and the self-hosted runner are **trusted
  hardware evidence, not cryptographic attestation**. A malicious runner or
  misreporting driver can fake them. Small-model training does not demonstrate
  GPU performance advantage or distributed training.

## Run locally on a Linux GPU host

You need a Linux x86-64 machine or VM with an **attached GPU supported by a
working OpenCL driver/ICD**. Install a C++17 compiler, CMake, Git, `clinfo`,
the OpenCL ICD loader/development headers and the GPU vendor's driver.
Installing the loader alone **does not** make a GPU device available.

On Ubuntu/Debian, the build prerequisites can be installed using:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake git clinfo \
  ocl-icd-opencl-dev opencl-headers
```

Install and enable the GPU vendor's OpenCL driver separately, then check:

```sh
clinfo -l
```

From the repository root (on that GPU host):

```sh
bash scripts/ai-gpu-verify.sh gpu-evidence
```

The script configures OpenCL, refuses builds without the actual OpenCL
compiler flag, compiles the **GPU-required** test, executes GPU forward and
backward matrix multiplication, trains a two-layer model and evaluates it
through the `noqeri-ai` CLI. It writes `gpu-evidence/gpu-results.json`,
`gpu-evidence/clinfo.txt`, `gpu-evidence/environment.txt`,
`gpu-evidence/gpu-test.log` and training/evaluation logs.
It exits with an error if there is no OpenCL GPU or if any numerical or
training test fails. It does **not** quietly switch to a CPU backend.

## Run via GitHub Actions

The manual workflow is
[`.github/workflows/ai-gpu-hardware.yml`](../.github/workflows/ai-gpu-hardware.yml).

1. In the Noqeri repository, open **Settings → Actions → Runners → New
   self-hosted runner**. Select Linux/x64. Follow the GitHub-provided
   installation commands **on your GPU machine**. Only repository admins
   with the appropriate permissions can register a runner.
2. Give the runner the custom label **`noqeri-gpu`** in addition to its
   normal `self-hosted`, `linux`, and `x64` labels. Ensure the runner
   can reach GitHub and that `clinfo -l` lists the real GPU.
3. Open **Actions → Noqeri AI physical GPU training qualification → Run
   workflow**, select the trusted **`main`** branch and dispatch it.
4. Check that the job executes and completes with a green result (not
   `skipped` or indefinitely `queued`). Download the
   **`noqeri-physical-gpu-qualification`** artifact. Verify its
   `gpu-results.json` says `"result": "PASS"` and the recorded selected
   device type is `CL_DEVICE_TYPE_GPU`.

**Runner security:** The workflow is manual and limited to `main`.
Use a disposable or dedicated GPU environment if possible. Treat runner
registration tokens as secrets; never paste them into the repository,
issue comments or chat. Self-hosted runners execute checkout/build code
with the runner account's privileges, so do not register one on a shared,
sensitive workstation.

**Cost:** A rented GPU host or GitHub GPU larger runner may incur real
charges. This workflow does not rent or provision hardware and cannot
register your GPU for you. Nothing should be purchased without a deliberate
budget decision.

## When the job is still queued

The labels `[self-hosted, linux, x64, noqeri-gpu]` must match an **online,
idle runner accessible to this repository**. A queued workflow does not prove
that GPU tests passed. A standard `ubuntu-latest` GitHub-hosted runner
generally cannot replace a physical GPU runner for this test.

If you have no GPU machine and want a quick hardware check, provision a
short-lived GPU VM with OpenCL support, register it using GitHub's current
runner installation instructions, trigger the manual workflow on `main`,
download the evidence and shut down the VM to avoid further charges.
