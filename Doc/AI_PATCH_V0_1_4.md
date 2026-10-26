# Noqeri v0.1.4 AI development patch

**Status: experimental additive patch to the immutable public v0.1.4 developer preview.** This patch does not move the original `v0.1.4` git tag, does not replace previously published binaries, and does **not** claim modern LLM pretraining or general-purpose AGI. Every patch package records its exact post-tag source commit.

## Working capabilities

- `Lib/ai/learn.noqeri` — allocation-free typed Noqeri scalar linear prediction, bounded error gradients, dot product and single-example learning steps. These are normal language modules checked by the principal Noqeri compiler.
- `Lib/ai/agent.noqeri` — typed Noqeri budget and tool-approval helpers for small agent applications.
- `noqeri ai train dataset.csv model.nqai` — native, CPU-only seeded minibudget **binary logistic SGD** training on numeric CSV (1–32 feature columns, last column is binary label 0/1; no headers). Performs training-only feature normalization and a held-out validation split. Defaults: 160 epochs, learning rate 0.08, L2 regularization 0.0002, seed 42.
- `noqeri ai predict model.nqai ...features` — memory-bounded offline model loading/inference, with numeric validation.
- `noqeri ai agent model.nqai observations.csv --max-steps 5 [--allow-emit]` — local autonomous observation → inference → action loop. The only built-in capability is **emitting a decision to standard output**, and it is *denied by default*. Strict step and tool-call budgets, bounded CSV/model data, no shell, remote networking, arbitrary filesystem writes, or silent external account access.
- Reusable Apache-2.0 `libnoqeri_ai` library and `noqeri/ai.hpp` C++17 API so application developers can supply explicitly authorized tool callback integrations.

## Beginner example

```sh
# Create training.csv with numeric features and a final 0/1 label
noqeri ai train training.csv classifier.nqai --epochs 200 --seed 11
noqeri ai predict classifier.nqai 2.0
# observations.csv contains numeric feature rows WITHOUT a label
noqeri ai agent classifier.nqai observations.csv --max-steps 10
# Give the agent permission to print routed decisions:
noqeri ai agent classifier.nqai observations.csv --max-steps 10 --allow-emit

# Noqeri-language example uses imported typed stdlib:
noqeri check examples/ai/online_learning.noqeri
```

You can call `noqeri-ai` directly if the existing installed `noqeri` binary does not yet contain the `ai` subcommand. Patch packages include the updated compiler CLI and native AI executable together. No Python, Node.js, TensorFlow, PyTorch, GPU runtime, package registry or external model provider is needed.

## Model and policy boundaries

The `.nqai` version-one model is a small bounded text file containing learned weights, bias and training-only normalization parameters. It is intended for local development, **not an encrypted or signed model format**. Import arbitrary models only from trusted sources.

The agent does **not** interpret free-form LLM instructions, browse the internet, run a shell, access files, or directly authorize its own tools. Embedding applications can provide a `ToolCallback` but remain responsible for validation, sandboxing, credentials and human approval for consequential side effects. Classification accuracy only measures this sample binary task; it does not establish general AI intelligence.

Training limits: up to 100,000 rows, 32 numeric features, 10,000 epochs, 8,192 bytes/CSV line, no quoted/string CSV fields or GPU/distributed training. Seeds make training repeatable within the same compiler/platform runtime; cross-platform bit-for-bit floating-point agreement is not guaranteed.

## Patch vs original v0.1.4

The original `v0.1.4` release points to git SHA `50606b6c491dfde2c8a9f4db624a82f8839621b3`. AI implementation is **later work**. New, uniquely named `noqeri-v0.1.4-ai-patch-<platform>` downloads ship a `PATCH_SOURCE_SHA` file and a separate patch checksum/manifest. Do not claim the AI patch came from the original tag commit.

This is an additive **developer preview**. No foundation-model pretraining, transformer tensor autodifferentiation, tokenizer, LLM hosting, model-provider bindings, distributed training, GPU kernels or external tool-agent runtime is included. Those require separate implementations and qualification.
