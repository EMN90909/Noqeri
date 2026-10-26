#!/usr/bin/env python3
"""Dynamic routes, layout, CSRF and server-side validation; no browser JS."""
import http.client
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

binary = sys.argv[1]
source = pathlib.Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory() as folder:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1",0))
        port=probe.getsockname()[1]
    proc=subprocess.Popen([binary,"serve-app",str(source),str(port),
                           str(pathlib.Path(folder)/"state.nqkv")],
                          stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    def req(method,path,payload=None,cookie=None):
        conn=http.client.HTTPConnection("127.0.0.1",port,timeout=4)
        headers={}
        if payload is not None:headers["Content-Type"]="application/x-www-form-urlencoded"
        if cookie:headers["Cookie"]=cookie
        conn.request(method,path,body=payload,headers=headers)
        response=conn.getresponse()
        result=response.status,dict(response.getheaders()),response.read().decode()
        conn.close()
        return result
    try:
        for attempt in range(80):
            if proc.poll() is not None:
                raise AssertionError("native app exited: "+proc.stderr.read().decode())
            try:
                if req("GET","/theme.css")[0]==200:break
            except OSError:time.sleep(.05)
        else:raise AssertionError("native app did not start")

        status,headers,home=req("GET","/")
        assert status==200,(status,home)
        assert "Unified Noqeri" in home and "Noqeri independent full-stack preview" in home
        assert "<script" not in home.lower()
        assert home.count("nq-navbar")==1
        status,headers,member=req("GET","/members/Ada")
        assert status==200,(status,member)
        assert "Hello, Ada" in member
        assert "Last greeting: Welcome" in member
        assert member.count("Noqeri independent full-stack preview")==1
        sid=headers["Set-Cookie"].split(";",1)[0]
        assert req("GET","/members/Lina",cookie=sid)[2].find("Hello, Lina")!=-1
        assert req("GET","/members/not%2fallowed")[0]==400
        assert req("GET","/members/Ada/extra")[0]==404
        csrf=re.search(r'name="_nq_csrf" value="([^"]+)"',member).group(1)
        action=re.search(r'name="_nq_action" value="([^"]+)"',member).group(1)
        def body(message,csrf=csrf):
            return urllib.parse.urlencode({
                "_nq_action":action,"_nq_csrf":csrf,"message":message})
        status,_,invalid=req("POST","/members/Ada",body("A"),sid)
        assert status==422,(status,invalid)
        assert "validation failed" in invalid.lower()
        assert "Invalid length for message" in invalid
        status,_,unchanged=req("GET","/members/Ada",cookie=sid)
        assert "Submissions: 0" in unchanged
        status,_,_=req("POST","/members/Ada",body("<svg onload=alert(1)>"),sid)
        assert status==303,status
        status,_,updated=req("GET","/members/Ada",cookie=sid)
        assert status==200
        assert "Last greeting: &lt;svg onload=alert(1)&gt;" in updated
        assert "Submissions: 1" in updated
        assert req("POST","/members/Ada",body("Again"),sid)[0]==403
        newcsrf=re.search(r'name="_nq_csrf" value="([^"]+)"',updated).group(1)
        assert req("POST","/members/Lina",body("Hi!",newcsrf),sid)[0]==303
        assert "Submissions: 2" in req("GET","/members/Ada",cookie=sid)[2]
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill();proc.communicate()
print("PASS: layouts, dynamic routes, session state, CSRF, validation, escaped input")
