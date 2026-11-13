#!/usr/bin/env python3
"""Fail-closed integrity and provenance validation for v0.1.4 AI add-on SDKs."""
from verify_patch_common import run_cli

if __name__ == "__main__":
    run_cli(
        "verify_ai_patch.py",
        patch_slug="ai-patch",
        sum_slug="ai-patch",
        manifest_note="Post-tag patch. Does not move v0.1.4. No foundation-model or LLM training.",
        extra_required=[],
        readme_name="README-AI-PATCH.md",
    )
