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


    # Compare uninterrupted and resumed execution with identical total steps.
    full = root / "deep-full.nqtf"
    resumed = root / "deep-resumed.nqtf"
    checkpoint = root / "deep-train.nqck"
    deep = ("--optimizer", "adamw", "--layers", "3", "--precision", "fp16",
            "--steps", "24", "--batch", "2", "--rate", "0.003",
            "--warmup", "3", "--min-rate", "0.0001", "--schedule", "cosine",
            "--validation", "0.2", "--seed", "11", "--context", "4",
            "--hidden", "8", "--merges", "3")
    once, _ = run("transformer", "train", corpus, full, *deep)
    assert "layers=3" in once and "precision=fp16" in once
    paused, _ = run("transformer", "train", corpus, resumed, *deep,
                    "--checkpoint", checkpoint, "--stop-after", "9")
    assert "completed=0" in paused, paused
    assert checkpoint.stat().st_size > 0
    complete, _ = run("transformer", "train", corpus, resumed, *deep,
                      "--resume", checkpoint)
    assert "completed=1" in complete, complete
    assert full.read_bytes() == resumed.read_bytes(), "resume model differs from uninterrupted training"
    run("transformer", "train", corpus, resumed, *deep,
        "--rate", "0.004", "--resume", checkpoint, success=False)
    assert resumed.read_bytes() == full.read_bytes(), "invalid resume modified model"
    corrupted = root / "corrupt.nqck"
    corrupted.write_bytes(checkpoint.read_bytes()[:-5])
    run("transformer", "train", corpus, resumed, *deep,
        "--resume", corrupted, success=False)
    assert resumed.read_bytes() == full.read_bytes(), "corrupt checkpoint modified model"

print("PASS: AdamW, exact checkpoint resume, 3-layer FP16 CLI and validation")
