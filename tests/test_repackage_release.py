"""Prove release repacking requires real staged files and rejects unsafe tar archives."""
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

BASE=Path(__file__).resolve().parents[1]
SCRIPT=BASE/"scripts/distribution/repackage_release.py"

class ReleaseRepackingTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/"artifacts").mkdir()
        self.license=self.root/"LICENSE"
        self.license.write_text("GPL-3.0-only\n")
        self.archive=self.root/"artifacts/noqeri-v0.1.3-linux-x86_64.tar.gz"

    def make_tar(self, malicious=False, no_vm=False):
        with tarfile.open(self.archive,"w:gz") as tar:
            files={
                "stage/bin/noqeri":b"compiler-fixture",
                "stage/share/noqeri/Lib/std/hello.noqeri":b"module hello",
                "stage/include/noqeri/abi.h":b"/* header */",
            }
            if not no_vm: files["stage/bin/noqeri-nqri"]=b"bytecode VM"
            for name,data in files.items():
                info=tarfile.TarInfo(name)
                info.size=len(data)
                tar.addfile(info,io.BytesIO(data))
            if malicious:
                entry=tarfile.TarInfo("stage/../escape")
                entry.size=5
                tar.addfile(entry,io.BytesIO(b"wrong"))

    def run_script(self,good=True):
        r=subprocess.run([sys.executable,str(SCRIPT),
            "--input",str(self.root/"artifacts"),"--output",str(self.root/"dist"),
            "--version","0.1.3","--source-commit","a"*40,"--license",str(self.license),
            "--targets","linux-x86_64"],capture_output=True,text=True)
        if good:self.assertEqual(r.returncode,0,r.stderr)
        else:self.assertNotEqual(r.returncode,0)
        return r

    def test_compiler_and_sdk_are_really_packaged(self):
        self.make_tar()
        self.run_script()
        fragment=json.loads((self.root/"dist/linux-x86_64/fragment.json").read_text())
        self.assertEqual(len(fragment["assets"]),5)
        self.assertEqual(fragment["qualification"],"build-only")
        import zipfile
        z=next((self.root/"dist/linux-x86_64").glob("*.zip"))
        with zipfile.ZipFile(z) as contents:
            self.assertIn("noqeri-v0.1.3-linux-x86_64/bin/noqeri-nqri",contents.namelist())
            self.assertIn("noqeri-v0.1.3-linux-x86_64/include/noqeri/abi.h",contents.namelist())

    def test_native_installers_are_checksums_not_fake_links(self):
        self.make_tar()
        target="noqeri-0.1.3-linux-x86_64"
        (self.root/"artifacts"/(target+".deb")).write_bytes(b"test deb bytes")
        (self.root/"artifacts"/(target+".rpm")).write_bytes(b"test rpm bytes")
        result=subprocess.run([sys.executable,str(SCRIPT),
            "--input",str(self.root/"artifacts"),"--output",str(self.root/"dist"),
            "--version","0.1.3","--source-commit","a"*40,"--license",str(self.license),
            "--targets","linux-x86_64","--include-installers"],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        fragment=json.loads((self.root/"dist/linux-x86_64/fragment.json").read_text())
        types={a["format"] for a in fragment["assets"]}
        self.assertEqual(types,{"binary","gz","tar","tar.gz","zip","deb","rpm"})

    def test_missing_installers_fail_release(self):
        self.make_tar()
        result=subprocess.run([sys.executable,str(SCRIPT),
            "--input",str(self.root/"artifacts"),"--output",str(self.root/"dist"),
            "--version","0.1.3","--source-commit","a"*40,"--license",str(self.license),
            "--targets","linux-x86_64","--include-installers"],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn("missing, empty or unsafe native installer",result.stderr)

    def test_no_vm_is_rejected(self):
        self.make_tar(no_vm=True)
        self.run_script(False)

    def test_traversal_is_rejected(self):
        self.make_tar(malicious=True)
        self.run_script(False)

    def test_missing_artifacts_fail_closed(self):
        self.run_script(False)

if __name__=="__main__":unittest.main()
