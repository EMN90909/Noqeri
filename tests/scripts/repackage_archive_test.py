#!/usr/bin/env python3
"""Regression tests for safe SDK archive extraction in release repackaging."""
from __future__ import annotations
import importlib.util
import io
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

FILE = Path(__file__).resolve().parents[2] / "scripts/distribution/repackage_release.py"
spec = importlib.util.spec_from_file_location("repackage_release", FILE)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

CONTENT = {
    "stage/bin/noqeri": b"compiler",
    "stage/bin/noqeri-nqri": b"nqri-vm",
    "stage/share/noqeri/Lib/std/example.noqeri": b"print(1)",
    "stage/include/noqeri/example.hpp": b"#pragma once",
}


class RepackArchiveTests(unittest.TestCase):
    def test_root_entry_is_allowed_only_as_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / "noqeri-v0.1.3-linux-x86_64.tar.gz"
            with tarfile.open(path, "w:gz") as tar:
                directory = tarfile.TarInfo("stage/")
                directory.type = tarfile.DIRTYPE
                tar.addfile(directory)
                for name, value in CONTENT.items():
                    item = tarfile.TarInfo(name)
                    item.size = len(value)
                    tar.addfile(item, io.BytesIO(value))
            stage, executable = module.extract_sdk(path, "linux-x86_64", root / "out")
            self.assertEqual(executable.read_bytes(), b"compiler")
            self.assertEqual(stage.joinpath("bin/noqeri-nqri").read_bytes(), b"nqri-vm")

    def test_top_level_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / "noqeri-v0.1.3-linux-x86_64.tar.gz"
            with tarfile.open(path, "w:gz") as tar:
                entry = tarfile.TarInfo("stage")
                entry.size = 3
                tar.addfile(entry, io.BytesIO(b"bad"))
            with self.assertRaises(ValueError):
                module.extract_sdk(path, "linux-x86_64", root / "out")

    def test_zip_root_directory_allowed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / "noqeri-v0.1.3-windows-x86.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("stage/", "")
                for name, value in CONTENT.items():
                    if name.startswith("stage/bin/noqeri"):
                        name += ".exe"
                    archive.writestr(name, value)
            stage, executable = module.extract_sdk(path, "windows-x86", root / "out")
            self.assertEqual(executable.read_bytes(), b"compiler")
            self.assertEqual(stage.joinpath("bin/noqeri-nqri.exe").read_bytes(), b"nqri-vm")


if __name__ == "__main__":
    unittest.main()
