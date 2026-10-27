#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""End-to-end Noqeri AI CLI training, eval, failure behavior and reproducibility."""
import pathlib
import subprocess
import sys
import tempfile

ai = pathlib.Path(sys.argv[1]).resolve()

def run(*args, success=True):
    result = subprocess.run([str(ai), *map(str, args)],
                            text=True, capture_output=True, timeout=120)
    if success and result.returncode:
        raise AssertionError(f"{args}: {result.stderr}")
    if not success and not result.returncode:
        raise AssertionError(f"expected failure for {args}")
    return result.stdout, result.stderr

with tempfile.TemporaryDirectory(prefix="noqeri-adamw-") as directory:
    root = pathlib.Path(directory)
    corpus = root / "corpus.txt"
    corpus.write_text("hello hello world\n" * 10, encoding="ascii")
    model_a = root / "a.nqtf"
    model_b = root / "b.nqtf"
    flags = (
        "--steps", "24", "--context", "3", "--hidden", "6",
        "--merges", "3", "--optimizer", "adamw", "--batch", "4",
        "--rate", "0.003", "--clip", "1", "--weight-decay", "0.01",
        "--schedule", "cosine", "--warmup", "3", "--min-rate", "0.0001",
        "--validation", "0.2", "--seed", "7", "--backend", "scalar"
    )
    output, _ = run("transformer", "train", corpus, model_a, *flags)
    assert "optimizer=adamw" in output and "batch=4" in output, output
    assert "validation_loss=" in output and "validation_perplexity=" in output, output
    run("transformer", "train", corpus, model_b, *flags)
    assert model_a.read_bytes() == model_b.read_bytes(), "nondeterministic AdamW training"
    evaluated, _ = run("transformer", "evaluate", model_a, corpus)
    assert "eval_loss=" in evaluated and "perplexity=" in evaluated, evaluated
    generated, _ = run("transformer", "generate", model_a, "hello", "--tokens", "4")
    assert "generated=" in generated, generated

    original = model_a.read_bytes()
    run("transformer", "train", corpus, model_a, "--optimizer", "rmsprop", success=False)
    assert model_a.read_bytes() == original
    run("transformer", "train", corpus, model_a, "--optimizer", "adamw",
        "--batch", "17", success=False)
    assert model_a.read_bytes() == original
    run("transformer", "train", corpus, model_a, "--validation", "0.7",
        success=False)
    assert model_a.read_bytes() == original
    run("transformer", "train", corpus, model_a, "--warmup", "200",
        "--steps", "20", success=False)
    assert model_a.read_bytes() == original

print("PASS: seeded batched AdamW, held-out evaluation, repeatability and CLI validation")
