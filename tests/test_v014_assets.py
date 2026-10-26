"""Unit qualification for v0.1.4 release artifact architecture and provenance."""
import json
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
VERIFIER=ROOT/"scripts/distribution/verify_v014_assets.py"
COMMIT="a"*40
TAG="b"*40

class AssetTests(unittest.TestCase):
    def elf(self, machine=62, width=2):
        contents=bytearray(160)
        contents[:4]=b"\x7fELF"
        contents[4]=width
        contents[5]=1
        struct.pack_into("<H",contents,18,machine)
        return contents

    def make(self, root):
        source=root/"stage"
        (source/"bin").mkdir(parents=True)
        (source/"share/noqeri/Lib/web").mkdir(parents=True)
        (source/"share/noqeri/licenses").mkdir(parents=True)
        (source/"bin/noqeri").write_bytes(self.elf())
        (source/"bin/noqeri-nqri").write_bytes(self.elf())
        (source/"bin/noqeri-web").write_bytes(self.elf())
        (source/"share/noqeri/Lib/web/ui.noqeri").write_text("component Test() {}")
        (source/"share/noqeri/LICENSE").write_text("GPL")
        (source/"share/noqeri/licenses/Apache-2.0.txt").write_text("Apache 2")
        for name in ("noqeri","noqeri-nqri"):
            (root/("noqeri-v0.1.4-linux-x86_64.bin" if name=="noqeri"
                    else "noqeri-nqri-v0.1.4-linux-x86_64.bin")).write_bytes(self.elf())
        (root/"noqeri-0.1.4-linux-x86_64.deb").write_bytes(b"fake Debian metadata for release format test"*5)
        (root/"noqeri-0.1.4-linux-x86_64.rpm").write_bytes(b"fake RPM metadata for release format test"*5)
        with tarfile.open(root/"noqeri-v0.1.4-linux-x86_64.tar.gz","w:gz") as tar:
            tar.add(source,arcname="stage")

    def verify(self, root):
        return subprocess.run([sys.executable,str(VERIFIER),str(root),COMMIT,TAG,"123"],
                              text=True,capture_output=True,timeout=10)

    def test_valid_verified_subset_and_manifest_provenance(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);self.make(p)
            result=self.verify(p)
            self.assertEqual(result.returncode,0,result.stderr)
            manifest=json.loads((p/"v0.1.4-platform-manifest.json").read_text())
            self.assertEqual(manifest["package_source_commit"],COMMIT)
            self.assertEqual(manifest["release_tag_commit"],TAG)
            self.assertEqual(manifest["available"],["linux-x86_64"])
            self.assertEqual(len(manifest["unavailable"]),15)
            self.assertIn("noqeri-v0.1.4-linux-x86_64.bin",(p/"SHA256SUMS-platform-v014").read_text())

    def test_reject_architecture_mislabel(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);self.make(p)
            (p/"noqeri-v0.1.4-linux-x86_64.bin").write_bytes(self.elf(machine=183))
            result=self.verify(p)
            self.assertNotEqual(result.returncode,0)
            self.assertIn("machine",result.stderr)

    def test_refuse_partial_sdk_group(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);self.make(p)
            (p/"noqeri-nqri-v0.1.4-linux-x86_64.bin").unlink()
            self.assertIn("incomplete",self.verify(p).stderr)

    def test_refuse_unexpected_asset(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);self.make(p)
            (p/"noqeri-v0.1.4-linux-arm64.bin").write_bytes(self.elf(machine=183))
            self.assertIn("unexpected",self.verify(p).stderr)

if __name__=="__main__":unittest.main()
