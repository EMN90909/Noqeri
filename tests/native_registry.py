import http.server
import hashlib
import io
import json
import gzip
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import threading
import unittest
import sys

BIN=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(os.environ.get('NOQERI_BIN','noqeri'))
MANIFEST=b'package {\n name: "noqeri/widgets"\n version: "0.1.0"\n entry: "src/main.noqeri"\n}\n'
FILES={'package.noqeri':MANIFEST,'src/main.noqeri':b'module noqeri.widgets\n'}
def digest(files):
    h=hashlib.sha256()
    for name in sorted(files):h.update(name.encode()+b'\0'+files[name]+b'\0')
    return 'sha256:'+h.hexdigest()
def archive(files):
    data=io.BytesIO()
    with tarfile.open(fileobj=data,mode='w',format=tarfile.USTAR_FORMAT) as tar:
        for name,b in sorted(files.items()):
            meta=tarfile.TarInfo(name);meta.size=len(b);meta.mode=0o644
            tar.addfile(meta,io.BytesIO(b))
    return gzip.compress(data.getvalue(),mtime=0)
PKG=archive(FILES)
INTEGRITY='sha256:'+hashlib.sha256(PKG).hexdigest()
class Handler(http.server.BaseHTTPRequestHandler):
    corrupt=False
    def do_GET(self):
        if self.path=='/v1/index':
            output={'protocol':'v1','packageFormat':'nqpkg-1','packages':[{'namespace':'noqeri','name':'widgets','versions':[{'version':'0.1.0','yanked':False}]}]}
        elif self.path=='/v1/packages/noqeri/widgets/0.1.0':
            output={'version':'0.1.0','yanked':False,'entry':'src/main.noqeri','archiveIntegrity':INTEGRITY}
        elif self.path=='/v1/packages/noqeri/widgets/0.1.0/download':
            data=PKG if not self.corrupt else PKG[:-10]+b'0123456789'
            self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
        elif self.path=='/v1/advisories':output={'schema':1,'advisories':[]}
        else: self.send_response(404);self.end_headers();return
        data=json.dumps(output).encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*args): pass
class NativeTests(unittest.TestCase):
    def setUp(self):
        self.t=tempfile.TemporaryDirectory();self.addCleanup(self.t.cleanup);self.base=Path(self.t.name)
        self.home=self.base/'home';self.project=self.base/'project';self.project.mkdir();
        (self.project/'project.nqon').write_text('name = "demo"\nversion = "0.1.0"\n');(self.project/'main.noqeri').write_text('import package "noqeri/widgets"\n')
        self.server=http.server.HTTPServer(('127.0.0.1',0),Handler);Handler.corrupt=False
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start();self.addCleanup(self.server.server_close);self.addCleanup(self.server.shutdown)
        self.env={**os.environ,'NOQERI_HOME':str(self.home),'NOQERI_REGISTRY_API_URL':f'http://127.0.0.1:{self.server.server_port}'}
    def cmd(self,*args,okay=True,offline=False):
        env={**self.env,**({'NOQERI_OFFLINE':'1'} if offline else {})}
        p=subprocess.run([str(BIN),'pkg',*args],cwd=self.project,env=env,text=True,capture_output=True,timeout=15)
        if okay:self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        else:self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)
        return p
    def test_full_flow(self):
        self.cmd('add','noqeri/widgets@0.1.0')
        manifest=(self.project/'project.nqon').read_text()
        self.assertIn('widgets = "noqeri/widgets@0.1.0"',manifest)
        self.assertIn('sha256:',(self.project/'noqeri.lock').read_text())
        self.cmd('install',offline=True)
        self.assertIn('src/main.noqeri',self.cmd('resolve','main.noqeri',offline=True).stdout)
        self.cmd('vendor',offline=True)
        self.assertTrue((self.project/'.noqeri/vendor-home/cache/registry-v1/noqeri/widgets/0.1.0/package.noqeri').exists())
        self.cmd('audit')
    def test_reject_corrupt_archive(self):
        Handler.corrupt=True
        p=self.cmd('add','noqeri/widgets',okay=False)
        self.assertIn('SHA-256',p.stderr)
        self.assertNotIn('widgets =',(self.project/'project.nqon').read_text())
    def test_reject_symlink_tamper(self):
        self.cmd('add','noqeri/widgets')
        file=self.home/'cache/registry-v1/noqeri/widgets/0.1.0/src/main.noqeri';file.unlink();file.symlink_to(self.project/'main.noqeri')
        self.assertIn('unsafe',self.cmd('install',offline=True,okay=False).stderr)
    def test_offline_missing_registry(self):
        p=self.cmd('add','noqeri/widgets',offline=True,okay=False)
        self.assertIn('offline',p.stderr)
    def test_offline_flags_fail_closed(self):
        # Even with online registry configuration, explicit --offline must
        # never download a missing package or claim a fresh advisory audit.
        (self.project/'project.nqon').write_text(
            'name = "demo"\\nversion = "0.1.0"\\n'
            'packages {\\n  widgets = "noqeri/widgets@0.1.0"\\n}\\n'
        )
        failed=self.cmd('vendor','--offline',okay=False)
        self.assertIn('offline',failed.stderr)
        self.assertFalse((self.home/'cache/registry-v1/noqeri/widgets').exists())
        failed=self.cmd('audit','--offline',okay=False)
        self.assertIn('offline',failed.stderr)

    def test_without_node_bin(self):
        self.env['NOQERI_NODE_BIN']='/no/such/node'
        self.cmd('add','noqeri/widgets@0.1.0')

    def test_disallow_external_http(self):
        self.env['NOQERI_REGISTRY_API_URL']='http://example.net'
        p=self.cmd('add','noqeri/widgets',okay=False)
        self.assertIn('HTTPS',p.stderr)
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])