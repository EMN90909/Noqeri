#!/usr/bin/env python3
"""Fail-closed integrity and provenance validation for v0.1.4 AI framework add-on SDKs."""
from verify_patch_common import run_cli

if __name__ == "__main__":
    run_cli(
        "verify_ai_framework_patch.py",
        patch_slug="ai-framework",
        sum_slug="ai-framework",
        manifest_note="Additive v0.1.4 AI framework. Reverse-mode tensors, BPE and one-layer causal transformer on CPU. Not large-scale production LLM pretraining or GPU.",
        extra_required=[
            "patch/include/noqeri/lm.hpp",
            "patch/include/noqeri/ai_framework.hpp",
            "patch/share/noqeri/Lib/ai/framework.noqeri",
            "patch/share/noqeri/Lib/std/numeric.noqeri",
        ],
        readme_name="README-AI-FRAMEWORK.md",
    )
