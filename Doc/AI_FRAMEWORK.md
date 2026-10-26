# Noqeri AI Framework (experimental 0.1.4 add-on)

Noqeri is a **general-purpose** programming language. This framework is an
optional set of Apache-2.0 library APIs, not an AI-specific syntax or compiler
mode. The existing `noqeri ai train/predict/agent` and `ai lm` commands
remain backward compatible.

## Components and implementation boundaries

| Component | Implemented | Boundary |
| --- | --- | --- |
| Portable Noqeri source | `Lib/ai/framework.noqeri`: scalar affine gradients, bounded SGD, pair frequencies, attention dot product, agent budget guards | These routines are the part directly importable in normal `.noqeri` source. |
| Reverse-mode autodiff | `noqeri/ai_framework.hpp`, `Tape`: tensor add, matmul, transpose, scale, tanh, softmax, causal softmax, gather, last-row, cross-entropy, backward | Small dense 2D CPU graph, up to 128 operations; C++17 API, **not directly callable through native Noqeri source import**. |
| Transformer training | Learn embeddings, Q/K/V projections, causal attention, attention output, residual, tanh feed-forward, second residual, output logits. Seeded SGD with gradients from Tape. | Single head and layer, bounded CPU model; not a production GPT architecture. |
| Subword tokenizer | Deterministic byte-pair merges and round-trip encode/decode; saves vocabulary together with model | Printable ASCII + LF only, up to 32 learned merges; not Unicode/BPE/WordPiece production coverage. |
| Numerical backends | Scalar reference, cache-tiled CPU, x86 SSE2 vectorized inner loop, Auto selection | SIMD compile-time availability; no CUDA, ROCm, Metal, Vulkan or verified performance speedup. |
| Agents | Existing capability-bounded, default-deny classifier agent with explicit callback permissions | No unsandboxed LLM tool execution, untrusted shell or network. |

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
language surface **only**. Native tensors, transformer training, and BPE are
available through the Noqeri compiler distribution's C++ SDK/CLI, not by
importing this package alone.

## Verification

- Forward-pass scalar/tiled/SSE2 parity, deterministic tokenizer round trips.
- Autodiff gradient check versus finite differences, causal masking.
- Train/save/load/generate model reproducibility and rejection of malformed
  model/input/budget values.
- Type-check/run portable Noqeri source on the regular compiler.
- Native cross-platform CI plus Go registry catalog/archive CI. Only passing
  CI targets should be claimed as verified.

The framework is **experimental**. It does not implement full transformer
pretraining, tokenizer Unicode support, multi-head attention, layer norm,
AdamW, mixed precision, quantization, distributed training, accelerator
kernels, or full Noqeri compiler-level bindings. It is a real but bounded
foundation for building those features without changing Noqeri's general-
purpose language grammar.
