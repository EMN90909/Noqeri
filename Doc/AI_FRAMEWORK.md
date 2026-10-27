# Noqeri AI Framework (experimental 0.1.4 add-on)

Noqeri is a **general-purpose** programming language. This framework is an
optional set of Apache-2.0 library APIs, not an AI-specific syntax or compiler
mode. The existing `noqeri ai train/predict/agent` and `ai lm` commands
remain backward compatible.

## Components and implementation boundaries

| Component | Implemented | Boundary |
| --- | --- | --- |
| Portable Noqeri source | `Lib/ai/framework.noqeri`: scalar affine gradients, bounded SGD, pair frequencies, attention dot product, agent budget guards | Portable helpers are directly importable; an additional explicit native AOT module exposes bounded GEMM, softmax, SGD, Tape graph execution, ASCII BPE and transient transformer training/generation. |
| Reverse-mode autodiff | `noqeri/ai_framework.hpp`, `Tape`: tensor add, matmul, transpose, scale, tanh, ReLU, GELU, layer normalization, softmax, causal softmax, gather, last-row, cross-entropy, backward | Small dense 2D CPU graph, up to 128 operations; The C++17 SDK provides the complete graph; a bounded subset of Tape operations is exposed through an explicitly linked Noqeri C ABI. |
| Transformer training | Train 1–4 stacked single-head causal attention blocks with AdamW/SGD, seeded mini-batches, validation, checkpoint/restart and f16/f32/f64 compute. | Bounded local research trainer; not a production GPT architecture. |
| Subword tokenizer | Deterministic byte-pair merges and round-trip encode/decode; saves vocabulary together with model | Printable ASCII + LF only, up to 32 learned merges; not Unicode/BPE/WordPiece production coverage. |
| Numerical backends | Scalar reference, cache-tiled CPU, x86 SSE2, FP32/FP16-rounded GEMM and optional OpenCL GPU GEMM | Accelerator needs physical OpenCL GPU for verification; no CUDA, ROCm, Metal or verified speedup. |
| Agents | Existing capability-bounded, default-deny classifier agent with explicit callback permissions | No unsandboxed LLM tool execution, untrusted shell or network. |

## Expanded matrix numerical layer

The additional bounded matrix operations and native-AOT C ABI are documented
in [AI_MATRIX_ADVANCED.md](AI_MATRIX_ADVANCED.md). The layer includes stable
probability reductions, GELU, layer normalization, matrix solving and
Cholesky decomposition. Existing transformer-training limits are unchanged.

## Deeper CPU training (opt-in)

The CPU single-head research transformer now supports **stateful AdamW**
(Adam with decoupled weight decay and bias-corrected moments), mini-batch
gradient accumulation across up to 16 teacher-forced next-token samples,
global L2 gradient norm clipping, warmup and cosine learning-rate scheduling,
and an optional held-out validation split.

These options extend the existing trainer without changing the default
optimizer (SGD, batch size 1). The training loop counts **optimizer updates**
as steps, so total sampled targets equals steps × batch size. The combined
budget is capped at 160,000 sampled targets. AdamW defaults to the usual
`beta1=0.9`, `beta2=0.999`, and `eps=1e-8`, with C++ customization
available through `AdamWOptions`. The AdamW update is staged across all
parameter groups; a failed update does not mutate any weight, moment or step
counter. Invalid gradients fail closed.

```sh
noqeri ai transformer train corpus.txt tuned.nqtf \
  --optimizer adamw --batch 4 --steps 500 --rate 0.001 \
  --clip 1 --weight-decay 0.01 \
  --warmup 25 --schedule cosine --min-rate 0.00001 \
  --validation 0.1 --seed 42

noqeri ai transformer evaluate tuned.nqtf evaluation.txt
noqeri ai transformer generate tuned.nqtf "hello" --tokens 24
```

Validation reserves the final requested fraction of *bytes* (0..0.3)
**before BPE fitting and model training**. The validation partition must
contain at least 16 bytes, and a deterministic teacher-forced sample of
at most 256 validation token positions is evaluated after training.
The reported perplexity is `exp(mean_cross_entropy)`; it is not a
generation-quality, ranking, or general-language benchmark. Evaluation text
is always supplied independently and is never used for weight updates.
Holdout and training samples share an ASCII/LF token domain but not their
training positions. If validation is disabled, no held-out metric is claimed.

The learning rate schedule decays to `--min-rate` after warmup. Use
`--schedule constant` (default) to keep a fixed rate after optional warmup.
For AdamW, a much smaller rate such as 0.001–0.003 is usually a better
starting point than the existing SGD default of 0.025. These are tuning
starting points, **not guaranteed optimal learning rates**.
The default SGD path intentionally keeps its legacy elementwise gradient
clipping behavior for compatibility; use `--batch` or `--clip` to opt
into the new global-norm clipping path.

`NQTF1` and `NQTF2` serialize model weights and merges only; resumable training state lives in a distinct `.nqck` checkpoint. See the sections below.

## Resumable multi-layer training and compute precision

The optional AI framework supports **1–4 stacked causal self-attention
blocks** through `--layers N` and stores models with 2–4 blocks in
`NQTF2`. Single-layer `NQTF1` files remain readable and writable.
Each stacked block has distinct Q/K/V, attention output and two
feed-forward matrices. This is still single-head attention per block,
not a full-scale production LLM architecture.

The `--precision fp64|fp32|fp16` option selects matrix-multiply
compute. FP32 and FP16 use **FP32 accumulation and FP64 master
weights/optimizer moments**, while FP16 operands are rounded to IEEE
binary16 precision in a portable CPU implementation. FP16 values that
overflow fail explicitly. These are genuine distinct numerical
compute paths, but CPU FP16 emulation is not evidence of a speedup or
GPU Tensor Core acceleration. Use FP64 for numerical reference tests.

```sh
noqeri ai transformer train corpus.txt model.nqtf \
  --layers 3 --precision fp16 --optimizer adamw \
  --steps 500 --batch 4 --rate 0.003 --warmup 20 \
  --checkpoint model.nqck --stop-after 200

noqeri ai transformer train corpus.txt model.nqtf \
  --layers 3 --precision fp16 --optimizer adamw \
  --steps 500 --batch 4 --rate 0.003 --warmup 20 \
  --resume model.nqck
```

The first invocation completes 200 of the **500 planned updates**;
the second resumes all 500 with the same learning-rate schedule.
`NQCK1` stores full parameter tensors, tokenizer, optimizer first
and second moments, current update count, running trailing loss and
serialized `std::mt19937` RNG state. It binds the corpus and
hyperparameters with a 64-bit fingerprint. A checksum and length
check detect corruption; checkpoint replacement uses a same-directory
pending file followed by atomic rename on supported filesystems.
Checkpoints are saved at most every 25 steps and at the requested
stopping step; failures between snapshots can lose up to 24 updates.
This is **not** crash-consistent fsync journaling, authentication or
a distributed-training checkpoint. Always keep trusted backups.

The final `.nqtf` is a model for generation; `.nqck` is the
separate resumable training state. A resumed invocation must supply
the **same corpus, seed, optimizer, step target, model shape and
training hyperparameters**. Changing those fields rejects the
checkpoint. No Noqeri compiler or syntax changes are necessary.

The [physical GPU qualification instructions](AI_GPU_HARDWARE_QUALIFICATION.md) describe the independent GPU-only regression, selected device provenance, how to register a dedicated runner, and how to interpret GitHub artifacts. Hardware has not been verified merely by successfully compiling the SDK.

## Accelerator support and evidence limits

The optional `NOQERI_AI_OPENCL` CMake option locates an OpenCL SDK,
then builds an FP32 OpenCL matrix-multiply kernel for forward and
reverse-mode gradient matrix multiplications.
`--backend opencl --precision fp32` requires an actual
`CL_DEVICE_TYPE_GPU` OpenCL device. If the runtime or GPU is missing,
it **fails with an error**, never a silently substituted CPU backend.
Non-matrix ops, optimizer steps and host training coordination
currently run on the CPU, so this is **GPU-assisted training**
rather than end-to-end GPU residency or full mixed-precision GPU
training. It carries substantial PCIe/setup overhead on small
models and should not be presumed faster.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DNOQERI_AI_OPENCL=ON
cmake --build build --target noqeri-ai noqeri-ai-resume-depth-test
ctest --test-dir build -R '^noqeri-ai-resume-depth

```sh
noqeri ai transformer train corpus.txt scratch.nqtf --steps 200 --context 4 --hidden 8 --merges 8 --backend auto
noqeri ai transformer generate scratch.nqtf "hello" --tokens 24
noqeri ai transformer generate scratch.nqtf "hello" --tokens 24 --temperature 0.7 --seed 42
```

`transformer train` validates an input corpus (16–65536 ASCII/LF bytes),
supports hidden sizes 2–24, context 1–16 and 1–10000 optimization steps.
`--backend` may be `auto`, `scalar`, `tiled` or `simd`.
Requesting SIMD on a CPU/build without it fails rather than silently
misrepresenting acceleration. `--rate` accepts rates between 0.000001
and 0.1. Text generation is limited to 64 tokens per call.

`*.nqtf` stores versioned model weights and BPE merges. It is a bounded,
non-executable **plain-text** research serialization, not GGUF, PyTorch,
ONNX, or a signed model format. Never load untrusted data without an OS
sandbox. `*.nqlm` (the older small character model) is a separate format.

## Use native tensors in an embedding application

```cpp
#include <noqeri/ai_framework.hpp>
using namespace noqeri_ai;
using namespace noqeri_ai::framework;

Matrix x(1,2); x.at(0,0)=2.0; x.at(0,1)=3.0;
Matrix weights(2,2);
Tape tape;
auto lhs=tape.leaf(x), rhs=tape.leaf(weights);
auto logits=tape.matmul(lhs,rhs);
auto loss=tape.cross_entropy(logits,1);
tape.backward(loss);
sgd_update(weights,tape.grad(rhs),0.01);
```

Link against the installed `libnoqeri_ai` (a C++17 static library). CMake
builds tests via `noqeri-ai-framework-test`. The API does not require Python,
TensorFlow, PyTorch or external model hosting; it is **not a substitute** for
their mature accelerator, dataset and distributed-training ecosystems.

## Noqeri Registry

Source-only package: **`noqeri/ai-framework@0.1.4`** in
[EMN90909/noqeri-registry](https://github.com/EMN90909/noqeri-registry).
The registry v1 index is read-only and produces deterministic `.nqpkg`
archives with SHA-256 verification. Import the installed
`src/framework.noqeri` through Noqeri's normal package resolution.

Because the Stage-0 registry importer cannot link native C++ into Noqeri
programs, installing the registry package currently supplies the portable
language surface **only**. The native tensor/autodiff/BPE/transformer research bindings require a
separate explicit link to `libnoqeri_ai`. The package alone remains
source-only; complete reusable models and full Tape APIs remain SDK features.

## Verification

- Forward-pass scalar/tiled/SSE2 parity, deterministic tokenizer round trips.
- Autodiff gradient check versus finite differences, causal masking.
- Train/save/load/generate model reproducibility and rejection of malformed
  model/input/budget values.
- Type-check/run portable Noqeri source on the regular compiler.
- Native cross-platform CI plus Go registry catalog/archive CI. Only passing
  CI targets should be claimed as verified.

The framework is **experimental**. It does not implement full transformer
pretraining, tokenizer Unicode support, multi-head attention, transformer-integrated layer norm,
end-to-end accelerator training, distributed training, or complete Noqeri compiler-level bindings. It is a real but bounded
foundation for building those features without changing Noqeri's general-
purpose language grammar.

## Direct native bindings (Linux x86-64 AOT preview)

Ordinary Noqeri programs can import `Lib/ai/native.noqeri` and explicitly
link `libnoqeri_ai` with `scripts/native-build.mjs --ai-framework`.
The source API now exposes bounded GEMM, row-wise or causally masked
softmax, and gradient-clipped SGD through caller-owned buffers. Each
operation returns a status and rejects invalid dimensions, capacities,
modes and nonfinite inputs without partially modifying the output.
These three bindings are [end-to-end qualified in native Noqeri AOT](https://github.com/EMN90909/Noqeri/actions/runs/40373362461), including capacity/error behavior; they do not yet execute under the Stage-2 NQRI VM or Stage-0
reference interpreter. Complete autodifferentiation graphs and model
training still require the native C++ SDK/CLI.

## Bounded differentiable graphs from ordinary Noqeri (AOT preview)

`ai_native_tape(&request)` accepts a caller-owned graph of at most 64
topologically ordered `AiTapeStep` records. The available operations
are leaf, add, matmul, transpose, scale, tanh, softmax (optionally causal),
cross entropy, and last row. The call constructs and destroys a C++ Tape,
can execute backpropagation from a cross-entropy loss and writes the selected
output and gradient to caller buffers only after all operations succeed.
Each leaf is restricted to 128x128 elements, and the sum of leaf elements
is bounded. Invalid shapes, forward references, non-finite values and
insufficient buffers fail without writing partial outputs. See
`examples/simplicity/ai_native_tape.noqeri`.
This is **not** automatic compiler-managed tensors, reusable graph handles,
Stage-2 VM integration, native source-level closures, or full transformer
training in Noqeri.

## Native Noqeri AdamW optimizer step (explicit state)

Ordinary native-AOT Noqeri applications can import
`Lib/ai/optimizer_native.noqeri`, populate an `AiAdamWRequest`,
and call `ai_native_adamw(&request)`. The C++ implementation is
`nq_ai_adamw_step` in `Runtime/native/ai_adamw_c.cpp`; its
public ABI is in `Include/noqeri/ai_framework_c.h`.

The **caller** owns the parameter buffer, gradients, first moments,
second moments, and three disjoint output buffers. Call `step=1`
for the first update, and increment it only after status `0`.
Persist all three output arrays yourself if you need to resume
optimization between calls. A training checkpoint still needs the
corresponding model architecture, tokenizer, data split, RNG and
scheduler position. No built-in resumable `.nqtf` checkpoint is claimed.

The native function clips the supplied gradient vector by global L2
norm before AdamW, uses bias-corrected moments and decoupled weight
decay, and stages every output. Nonzero status means **none of the
three outputs are changed**. The input buffers can alias the
corresponding output regions, but the three output regions may not
overlap. No process-global handle is stored. The maximum call size
is 1,048,576 f64 entries. See
`examples/simplicity/ai_native_adamw.noqeri`.

This binding is a **Linux x86-64 Noqeri native AOT preview**, not a
generic Stage-2 VM operator or an online model service. Its ABI tests
are in `tests/cpp/ai_adamw_c_test.cpp` and the native-AOT CI workflow.

## Differentiable neural building blocks

The `Tape` reverse-mode engine now exposes `relu`, approximate `gelu`
and `layer_norm` alongside the existing tensor operations. GELU uses the
tanh approximation, and layer normalization applies independently to each
row with a positive epsilon. Both support analytic backward gradients;
the tests compare GELU/LayerNorm derivatives against central finite
differences. Zero-input ReLU uses the derivative convention `0`.

Native AOT Noqeri graphs can use opcodes **9=ReLU, 10=GELU,
11=LayerNorm**, where `AiTapeStep.scalar` supplies the epsilon for
LayerNorm. See `examples/simplicity/ai_native_deep_tape.noqeri` and
`tests/cpp/ai_autodiff_deep_test.cpp`.

These operators enable custom experiments, but the serialized `.nqtf`
single-head model still uses its original fixed forward architecture.
Adding the new operators does not silently alter model compatibility or
retrofit the training CLI with arbitrary depth.

## Native BPE and bounded single-call transformer training

`ai_native_bpe_encode` provides deterministic ASCII/LF tokenization into a
caller-owned token buffer. `ai_native_transformer_train_generate` trains a
small CPU causal transformer from explicit caller-owned bytes, generates a
bounded response and returns the training loss. Model ownership is local to
the invocation; no network access, file I/O, leaked pointers or persistent
model handles. Validation failures leave all outputs untouched.

These remain experimental single-call AOT research APIs, **not** Unicode
tokenization, reusable model training, a GPU system or portable NQRI calls.
See `examples/simplicity/ai_native_tokenizer_transformer.noqeri`.

## Hosted-native reusable model handles (Linux x86-64 AOT)

Three separate calls now expose model ownership:
`ai_native_model_train(&train)` returns a nonzero opaque `u64` token,
`ai_native_model_generate(&request)` reuses that model for generation, and
`ai_native_model_release(handle)` explicitly frees the registered session.
Tokens are **process-local IDs, not pointers** and are never reused. At
most 16 models can coexist; training, handle lookup and release are
mutex-protected. A generation that acquires a model before a simultaneous
release safely keeps it alive through completion; later lookups fail with
status `-7`. Request validation and the capacity error `-3` leave caller
output unchanged; no exception crosses the C ABI.

This is **not** a persistent model file handle, distributed server,
thread-safe retraining of the same model, GPU implementation, or NQRI VM
host service. Training is an explicit bounded CPU operation and does not
expose all weights or graph internals. See
`examples/simplicity/ai_native_model_session.noqeri`.

## Latest native-AOT qualification

The [model-session CI workflow](https://github.com/EMN90909/Noqeri/actions/runs/40399043109)
passed both C ABI and ordinary Noqeri AOT tests for train, multiple
generations, generation into an undersized buffer, release, stale-handle
rejection and model reuse. These are library-level CPU operations and do
not add Noqeri language syntax or Stage-2 VM services.
 --output-on-failure
noqeri ai transformer train corpus.txt gpu.nqtf \
  --backend opencl --precision fp32 --optimizer adamw \
  --layers 2 --steps 30 --rate 0.003
```

On a machine with physical GPU + OpenCL installed, the regression
test exercises GPU forward GEMM and a trained 2-block model. On CI
workers without GPUs, the test instead confirms explicit GPU
selection fails. **An actual GPU test cannot be claimed as verified
until a runner with a GPU executes and passes that path.**

## Framework CLI

```sh
noqeri ai transformer train corpus.txt scratch.nqtf --steps 200 --context 4 --hidden 8 --merges 8 --backend auto
noqeri ai transformer generate scratch.nqtf "hello" --tokens 24
noqeri ai transformer generate scratch.nqtf "hello" --tokens 24 --temperature 0.7 --seed 42
```

`transformer train` validates an input corpus (16–65536 ASCII/LF bytes),
supports hidden sizes 2–24, context 1–16 and 1–10000 optimization steps.
`--backend` may be `auto`, `scalar`, `tiled` or `simd`.
Requesting SIMD on a CPU/build without it fails rather than silently
misrepresenting acceleration. `--rate` accepts rates between 0.000001
and 0.1. Text generation is limited to 64 tokens per call.

`*.nqtf` stores versioned model weights and BPE merges. It is a bounded,
non-executable **plain-text** research serialization, not GGUF, PyTorch,
ONNX, or a signed model format. Never load untrusted data without an OS
sandbox. `*.nqlm` (the older small character model) is a separate format.

## Use native tensors in an embedding application

```cpp
#include <noqeri/ai_framework.hpp>
using namespace noqeri_ai;
using namespace noqeri_ai::framework;

Matrix x(1,2); x.at(0,0)=2.0; x.at(0,1)=3.0;
Matrix weights(2,2);
Tape tape;
auto lhs=tape.leaf(x), rhs=tape.leaf(weights);
auto logits=tape.matmul(lhs,rhs);
auto loss=tape.cross_entropy(logits,1);
tape.backward(loss);
sgd_update(weights,tape.grad(rhs),0.01);
```

Link against the installed `libnoqeri_ai` (a C++17 static library). CMake
builds tests via `noqeri-ai-framework-test`. The API does not require Python,
TensorFlow, PyTorch or external model hosting; it is **not a substitute** for
their mature accelerator, dataset and distributed-training ecosystems.

## Noqeri Registry

Source-only package: **`noqeri/ai-framework@0.1.4`** in
[EMN90909/noqeri-registry](https://github.com/EMN90909/noqeri-registry).
The registry v1 index is read-only and produces deterministic `.nqpkg`
archives with SHA-256 verification. Import the installed
`src/framework.noqeri` through Noqeri's normal package resolution.

Because the Stage-0 registry importer cannot link native C++ into Noqeri
programs, installing the registry package currently supplies the portable
language surface **only**. The native tensor/autodiff/BPE/transformer research bindings require a
separate explicit link to `libnoqeri_ai`. The package alone remains
source-only; complete reusable models and full Tape APIs remain SDK features.

## Verification

- Forward-pass scalar/tiled/SSE2 parity, deterministic tokenizer round trips.
- Autodiff gradient check versus finite differences, causal masking.
- Train/save/load/generate model reproducibility and rejection of malformed
  model/input/budget values.
- Type-check/run portable Noqeri source on the regular compiler.
- Native cross-platform CI plus Go registry catalog/archive CI. Only passing
  CI targets should be claimed as verified.

The framework is **experimental**. It does not implement full transformer
pretraining, tokenizer Unicode support, multi-head attention, transformer-integrated layer norm,
AdamW, mixed precision, quantization, distributed training, accelerator
kernels, or complete Noqeri compiler-level bindings. It is a real but bounded
foundation for building those features without changing Noqeri's general-
purpose language grammar.

## Direct native bindings (Linux x86-64 AOT preview)

Ordinary Noqeri programs can import `Lib/ai/native.noqeri` and explicitly
link `libnoqeri_ai` with `scripts/native-build.mjs --ai-framework`.
The source API now exposes bounded GEMM, row-wise or causally masked
softmax, and gradient-clipped SGD through caller-owned buffers. Each
operation returns a status and rejects invalid dimensions, capacities,
modes and nonfinite inputs without partially modifying the output.
These three bindings are [end-to-end qualified in native Noqeri AOT](https://github.com/EMN90909/Noqeri/actions/runs/40373362461), including capacity/error behavior; they do not yet execute under the Stage-2 NQRI VM or Stage-0
reference interpreter. Complete autodifferentiation graphs and model
training still require the native C++ SDK/CLI.

## Bounded differentiable graphs from ordinary Noqeri (AOT preview)

`ai_native_tape(&request)` accepts a caller-owned graph of at most 64
topologically ordered `AiTapeStep` records. The available operations
are leaf, add, matmul, transpose, scale, tanh, softmax (optionally causal),
cross entropy, and last row. The call constructs and destroys a C++ Tape,
can execute backpropagation from a cross-entropy loss and writes the selected
output and gradient to caller buffers only after all operations succeed.
Each leaf is restricted to 128x128 elements, and the sum of leaf elements
is bounded. Invalid shapes, forward references, non-finite values and
insufficient buffers fail without writing partial outputs. See
`examples/simplicity/ai_native_tape.noqeri`.
This is **not** automatic compiler-managed tensors, reusable graph handles,
Stage-2 VM integration, native source-level closures, or full transformer
training in Noqeri.

## Native Noqeri AdamW optimizer step (explicit state)

Ordinary native-AOT Noqeri applications can import
`Lib/ai/optimizer_native.noqeri`, populate an `AiAdamWRequest`,
and call `ai_native_adamw(&request)`. The C++ implementation is
`nq_ai_adamw_step` in `Runtime/native/ai_adamw_c.cpp`; its
public ABI is in `Include/noqeri/ai_framework_c.h`.

The **caller** owns the parameter buffer, gradients, first moments,
second moments, and three disjoint output buffers. Call `step=1`
for the first update, and increment it only after status `0`.
Persist all three output arrays yourself if you need to resume
optimization between calls. A training checkpoint still needs the
corresponding model architecture, tokenizer, data split, RNG and
scheduler position. No built-in resumable `.nqtf` checkpoint is claimed.

The native function clips the supplied gradient vector by global L2
norm before AdamW, uses bias-corrected moments and decoupled weight
decay, and stages every output. Nonzero status means **none of the
three outputs are changed**. The input buffers can alias the
corresponding output regions, but the three output regions may not
overlap. No process-global handle is stored. The maximum call size
is 1,048,576 f64 entries. See
`examples/simplicity/ai_native_adamw.noqeri`.

This binding is a **Linux x86-64 Noqeri native AOT preview**, not a
generic Stage-2 VM operator or an online model service. Its ABI tests
are in `tests/cpp/ai_adamw_c_test.cpp` and the native-AOT CI workflow.

## Differentiable neural building blocks

The `Tape` reverse-mode engine now exposes `relu`, approximate `gelu`
and `layer_norm` alongside the existing tensor operations. GELU uses the
tanh approximation, and layer normalization applies independently to each
row with a positive epsilon. Both support analytic backward gradients;
the tests compare GELU/LayerNorm derivatives against central finite
differences. Zero-input ReLU uses the derivative convention `0`.

Native AOT Noqeri graphs can use opcodes **9=ReLU, 10=GELU,
11=LayerNorm**, where `AiTapeStep.scalar` supplies the epsilon for
LayerNorm. See `examples/simplicity/ai_native_deep_tape.noqeri` and
`tests/cpp/ai_autodiff_deep_test.cpp`.

These operators enable custom experiments, but the serialized `.nqtf`
single-head model still uses its original fixed forward architecture.
Adding the new operators does not silently alter model compatibility or
retrofit the training CLI with arbitrary depth.

## Native BPE and bounded single-call transformer training

`ai_native_bpe_encode` provides deterministic ASCII/LF tokenization into a
caller-owned token buffer. `ai_native_transformer_train_generate` trains a
small CPU causal transformer from explicit caller-owned bytes, generates a
bounded response and returns the training loss. Model ownership is local to
the invocation; no network access, file I/O, leaked pointers or persistent
model handles. Validation failures leave all outputs untouched.

These remain experimental single-call AOT research APIs, **not** Unicode
tokenization, reusable model training, a GPU system or portable NQRI calls.
See `examples/simplicity/ai_native_tokenizer_transformer.noqeri`.

## Hosted-native reusable model handles (Linux x86-64 AOT)

Three separate calls now expose model ownership:
`ai_native_model_train(&train)` returns a nonzero opaque `u64` token,
`ai_native_model_generate(&request)` reuses that model for generation, and
`ai_native_model_release(handle)` explicitly frees the registered session.
Tokens are **process-local IDs, not pointers** and are never reused. At
most 16 models can coexist; training, handle lookup and release are
mutex-protected. A generation that acquires a model before a simultaneous
release safely keeps it alive through completion; later lookups fail with
status `-7`. Request validation and the capacity error `-3` leave caller
output unchanged; no exception crosses the C ABI.

This is **not** a persistent model file handle, distributed server,
thread-safe retraining of the same model, GPU implementation, or NQRI VM
host service. Training is an explicit bounded CPU operation and does not
expose all weights or graph internals. See
`examples/simplicity/ai_native_model_session.noqeri`.

## Latest native-AOT qualification

The [model-session CI workflow](https://github.com/EMN90909/Noqeri/actions/runs/40399043109)
passed both C ABI and ordinary Noqeri AOT tests for train, multiple
generations, generation into an undersized buffer, release, stale-handle
rejection and model reuse. These are library-level CPU operations and do
not add Noqeri language syntax or Stage-2 VM services.
