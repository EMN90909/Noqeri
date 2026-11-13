#!/usr/bin/env python3
"""Fail-closed integrity and provenance validation for v0.1.4 LLM add-on SDKs."""
from verify_patch_common import run_cli

if __name__ == "__main__":
    run_cli(
        "verify_llm_patch.py",
        patch_slug="llm-foundation",
        sum_slug="llm-foundation",
        manifest_note="Post-tag extension; v0.1.4 unchanged. Tiny neural character LM and forward-only attention; not large-scale transformer LLM training.",
        extra_required=[
            "patch/include/noqeri/lm.hpp",
            "patch/share/noqeri/Lib/std/numeric.noqeri",
        ],
        readme_name="README-AI-PATCH.md",
    )
