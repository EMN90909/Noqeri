"""Offline contract tests for actual release archive bytes; no pretend compiler builds."""
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / "scripts" / "distribution.py"
COMMIT = "a" * 40

class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="noqeri-dist-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.exe = self.root / "noqeri"
        self.exe.write_bytes(b"ELF\x02synthetic-test-only")
        self.license = self.root / "LICENSE"
        self.license.write_text("GPL-3.0-only\n")
        self.smoke = self.root / "smoke.log"
        self.smoke.write_text("Simulated unit-test fixture (never publish this as real smoke evidence)\n")
        self.out = self.root / "dist"

    def call(self, *args, ok=True):
        proc = subprocess.run([sys.executable, str(TOOL), *map(str,args)],
                              capture_output=True, text=True,
                              env={**os.environ, "SOURCE_DATE_EPOCH": "315532800"})
        if ok:
            self.assertEqual(proc.returncode, 0, proc.stderr)
        else:
            self.assertNotEqual(proc.returncode, 0)
        return proc

    def bundle(self, target="linux", cpu="x86_64", qualification="build-only", smoke=None):
        return self.call("bundle", "--binary", self.exe, "--version", "0.1.3",
                         "--os", target, "--cpu", cpu, "--source-commit", COMMIT,
                         "--output", self.out, "--license", self.license,
                         "--qualification", qualification,
                         *(["--smoke-evidence", smoke] if smoke else []))

    def test_formats_roundtrip(self):
        self.bundle()
        target = self.out / "linux-x86_64"
        frag = json.loads((target / "fragment.json").read_text())
        self.assertEqual({a["format"] for a in frag["assets"]},
                         {"binary","gz","tar","tar.gz","zip"})
        self.assertEqual(frag["qualification"], "build-only")
        for info in frag["assets"]:
            file = target / info["filename"]
            self.assertEqual(hashlib.sha256(file.read_bytes()).hexdigest(), info["sha256"])
            if info["format"] == "zip":
                with zipfile.ZipFile(file) as z:
                    self.assertIn("noqeri-v0.1.3-linux-x86_64/bin/noqeri", z.namelist())
                    self.assertEqual(z.read("noqeri-v0.1.3-linux-x86_64/bin/noqeri"), self.exe.read_bytes())
            if info["format"] == "tar.gz":
                with tarfile.open(file) as t:
                    member = t.extractfile("noqeri-v0.1.3-linux-x86_64/bin/noqeri")
                    self.assertEqual(member.read(), self.exe.read_bytes())
        self.call("combine", "--input", self.out, "--version", "0.1.3",
                  "--source-commit", COMMIT, "--require", "linux-x86_64")
        result = json.loads((self.out / "release-manifest.json").read_text())
        self.assertEqual(result["targets"][0]["os"], "linux")
        self.assertEqual(result["channel"], "preview")

    def test_fail_closed_tampering_and_missing_matrix(self):
        self.bundle()
        self.call("combine", "--input", self.out, "--version", "0.1.3",
                  "--require", "windows-x86_64", ok=False)
        self.call("combine", "--input", self.out, "--version", "0.1.3",
                  "--stable", ok=False)
        asset = next((self.out / "linux-x86_64").glob("*.zip"))
        asset.write_bytes(asset.read_bytes() + b"tampered")
        self.call("combine", "--input", self.out, "--version", "0.1.3", ok=False)

    def test_do_not_fake_run_or_gpu_capability(self):
        self.call("bundle", "--binary", self.exe, "--os", "linux", "--cpu", "x86_64",
                  "--version", "0.1.3", "--source-commit", COMMIT,
                  "--output", self.out, "--qualification", "executed", ok=False)
        self.call("bundle", "--binary", self.exe, "--os", "linux", "--cpu", "x86_64",
                  "--version", "0.1.3", "--source-commit", COMMIT,
                  "--output", self.out, "--gpu-arch", "nvidia-sm_90", ok=False)

    def test_custom_os_tag_and_execute_evidence(self):
        self.bundle("custom-myos", "riscv64", "executed", self.smoke)
        self.call("combine", "--input", self.out, "--version", "0.1.3",
                  "--require", "custom-myos-riscv64", "--stable")
        result = json.loads((self.out / "release-manifest.json").read_text())
        self.assertTrue(result["targets"][0]["smoke_evidence"])

    def test_installed_sdk_modules_vm_and_symlinks(self):
        sdk=self.root/"stage"
        (sdk/"bin").mkdir(parents=True)
        (sdk/"share/noqeri/Lib/std").mkdir(parents=True)
        (sdk/"bin/noqeri").write_bytes(self.exe.read_bytes())
        (sdk/"bin/noqeri-nqri").write_bytes(b"real vm fixture")
        (sdk/"share/noqeri/Lib/std/hello.noqeri").write_text("module smoke\\n")
        self.call("bundle", "--binary", self.exe, "--sdk-root", sdk,
                  "--version", "0.1.3", "--os", "linux", "--cpu", "x86_64",
                  "--source-commit", COMMIT, "--output", self.out)
        archive=next((self.out/"linux-x86_64").glob("*.tar.gz"))
        with tarfile.open(archive) as tar:
            names=tar.getnames()
            self.assertIn("noqeri-v0.1.3-linux-x86_64/bin/noqeri-nqri",names)
            self.assertIn("noqeri-v0.1.3-linux-x86_64/share/noqeri/Lib/std/hello.noqeri",names)

    def test_reproducible_bytes(self):
        self.bundle()
        first = {(p.name, p.read_bytes()) for p in (self.out / "linux-x86_64").glob("*")}
        import shutil
        shutil.rmtree(self.out)
        self.bundle()
        second = {(p.name, p.read_bytes()) for p in (self.out / "linux-x86_64").glob("*")}
        self.assertEqual(first, second)

if __name__ == "__main__":
    unittest.main()
