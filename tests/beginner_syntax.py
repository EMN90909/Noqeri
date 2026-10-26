#!/usr/bin/env python3
"""Regression coverage for beginner syntax, inference, imports and repair hints."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler=Path(sys.argv[1]).resolve()
repo=Path(sys.argv[2]).resolve()
assert compiler.is_file() and (repo/"Lib"/"std"/"list.noqeri").is_file()
environment=dict(os.environ)
environment["NOQERI_STDLIB_ROOT"]=str((repo/"Lib").resolve())

def check(source, expected_code=None, as_json=True):
    with tempfile.TemporaryDirectory(prefix="nq-beginner-") as td:
        file=Path(td)/"main.noqeri"
        file.write_text(source,encoding="utf-8")
        args=[str(compiler),"check",str(file)]
        if as_json:args.append("--json")
        result=subprocess.run(args,env=environment,capture_output=True,text=True,timeout=25)
        if expected_code:
            assert result.returncode!=0,(source,result.stdout,result.stderr)
            if as_json:
                report=json.loads(result.stdout)
                assert report["schema"]==1 and report["ok"] is False,report
                matching=[x for x in report["diagnostics"] if x["code"]==expected_code]
                assert matching,(expected_code,report)
                error=matching[0]
                assert error["file"]==str(file),error
                assert error["span"]["line"]>=1 and error["span"]["column"]>=1,error
                assert error["help"] and len(error["help"])>14,error
                return report
            assert "error["+expected_code+"]" in result.stderr,result.stderr
            assert " | " in result.stderr and "help:" in result.stderr,result.stderr
            assert ".noqeri:" in result.stderr,result.stderr
        else:
            assert result.returncode==0,(source,result.stdout,result.stderr)
            if as_json:
                report=json.loads(result.stdout)
                assert report["schema"]==1 and report["ok"] is True,report
                assert report["diagnostics"]==[],report
        return result

# Inference is private, static and deterministic (explicit remains valid).
check('function square(value: int) = value * value\nprint(square(3))\n')
check('function positive(value: int) { return value > 0 }\nprint(positive(3))\n')
check('function greeting() = "hello"\nprint(greeting())\n')
check('function add(a: int,b: int): int { return a + b }\nprint(add(1,2))\n')
check('function sum_of(a: int,b: int) = a + b\nfunction twice(v: int) = sum_of(v,v)\nprint(twice(8))\n')
check('function square(value: int) = value * value\n',as_json=False)

# API stability and ambiguity: reject public, unknown-parameter or recursion.
check('export function exported() = 3\n',"NQR-T3061")
check('function mystery(x) = x\n',"NQR-T3060")
check('function recursive(x: int) = recursive(x)\n',"NQR-T3060")
check('function choose(x: int): bool { return x }\n',"NQR-T3005")

# Common classes of beginner errors provide code, location and concrete help.
for code,source in [
    ("NQR-T3002","let result = absent\n"),
    ("NQR-T3011","missing_function(1)\n"),
    ("NQR-T3012","function add(a: int,b: int): int { return a+b }\nadd(1)\n"),
    ("NQR-T3004","if 3 { print(3) }\n"),
    ("NQR-T3003","let x = 1\nlet x = 2\n"),
    ("NQR-T3035","let values = [1, true]\n"),
    ("NQR-M6002",'import "std/no_such_module"\n'),
    ("NQR-P2023",'import std/list\n')
]:
    check(source,code)
check('let result = absent\n',"NQR-T3002",as_json=False)

# The same quoted import syntax finds standard modules from an installed Lib.
check('import "std/list"\nfunction size_of(x: *List<int>): usize { return list_size(x) }\n')
check('import "std/set"\nfunction has_item(x: *Set<int>, n: int): bool { return set_has(x,n) }\n')
check('import "std/map"\nfunction size_of(x: *Map<int,int>): usize { return map_size(x) }\n')

# The shared result checks and provider facades must type-check from
# ordinary Noqeri modules, even when native host services are not linked.
check('import "std/result"\nfunction valid_status(): bool { return status_ok(0 as isize) }\n')
check('import "std/noesql"\nfunction ready(db: *Database): bool { return database_ok(db) }\n')
check('import "ai/native"\nfunction training_ok(status: isize): bool { return ai_native_ok(status) }\n')
check('import "std/http/client"\nfunction http_ready(status: int): bool { return http_client_response_ok(status) }\n')
check('import "std/file"\nfunction files_ok(status: isize): bool { return file_ok(status) }\n')
check('import "std/json"\nfunction json_valid(status: isize): bool { return json_ok(status) }\n')

# Ordinary list literals require neither usize casts nor manually sized buffers.
check('let values = [1, 2, 3]\nprint(len(values))\n')
print("PASS: private return inference, stable import paths, collection syntax and repair hints")
