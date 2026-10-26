#!/usr/bin/env python3
"""Exercise the real compiler and native training/agent CLI end-to-end."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

compiler=Path(sys.argv[1]).resolve()
runner=Path(sys.argv[2]).resolve()
example=Path(sys.argv[3]).resolve()
env=dict(os.environ,NOQERI_AI_BIN=str(runner))

def cli(*args,success=True):
    result=subprocess.run([str(compiler),"ai",*map(str,args)],env=env,
        text=True,capture_output=True,timeout=20)
    if success:
        assert result.returncode==0,(args,result.stdout,result.stderr)
    else:
        assert result.returncode!=0,(args,result.stdout,result.stderr)
    return result

# The language-level module compiles through the regular typed compiler,
# not a separate AI-specific source parser.
checked=subprocess.run([str(compiler),"check",str(example)],
                       text=True,capture_output=True,timeout=20)
assert checked.returncode==0,(checked.stdout,checked.stderr)

numeric_example=example.parent/"numeric_basics.noqeri"
check_numeric=subprocess.run([str(compiler),"check",str(numeric_example)],
                             text=True,capture_output=True,timeout=20)
assert check_numeric.returncode==0,(check_numeric.stdout,check_numeric.stderr)

with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    train=root/"training.csv"
    train.write_text("".join(f"{i/10:.1f},{1 if i>=0 else 0}\n"
                             for i in range(-40,40)),encoding="ascii")
    model=root/"classifier.nqai"
    report=cli("train",train,model,"--epochs","200","--seed","11")
    assert model.is_file() and "validation_accuracy" in report.stdout
    match=re.search(r"validation_accuracy=([0-9.]+)",report.stdout)
    assert match and float(match.group(1))>=0.85,report.stdout
    pos=cli("predict",model,"2.0")
    neg=cli("predict",model,"-2.0")
    assert "class=1" in pos.stdout and "class=0" in neg.stdout
    observations=root/"observations.csv"
    observations.write_text("2.0\n-2.0\n1.0\n",encoding="ascii")
    dry=cli("agent",model,observations,"--max-steps","2")
    assert "tools=0" in dry.stdout and "denied=2" in dry.stdout
    assert "tool=emit" not in dry.stdout
    applied=cli("agent",model,observations,"--max-steps","2","--allow-emit")
    assert "tools=2" in applied.stdout and "budget_exhausted=1" in applied.stdout
    assert "emit_positive" in applied.stdout and "emit_negative" in applied.stdout
    cli("agent",model,observations,"--max-steps","0","--allow-emit",success=False)
    cli("predict",model,"2","3",success=False)
    cli("train",train,root/"bad.nqai","--epochs","1000000",success=False)
    poisoned=root/"poison.csv"
    poisoned.write_text("1,1\nnan,0\n",encoding="ascii")
    cli("train",poisoned,root/"bad.nqai",success=False)
    # Ensure model and input formats do not execute payloads and do not open
    # external tools unless the user selected the sole 'emit' capability.
    malicious=root/"bad-model.nqai"
    malicious.write_text('NQAI1 1\n0\n1\nnan\n0\n',encoding="ascii")
    cli("predict",malicious,"1",success=False)

    # End-to-end neural character model through the same general-purpose CLI.
    corpus=root/"corpus.txt"
    corpus.write_text("hello world\n"*16,encoding="ascii")
    language_model=root/"small.nqlm"
    trained=cli("lm","train",corpus,language_model,"--steps","1500","--hidden","12",
                "--context","4","--seed","7")
    assert language_model.is_file() and "trailing_loss=" in trained.stdout
    emitted=cli("lm","generate",language_model,"hello","--tokens","16")
    again=cli("lm","generate",language_model,"hello","--tokens","16")
    assert emitted.stdout==again.stdout and emitted.stdout.startswith("generated=")
    cli("lm","generate",language_model,"hello","--tokens","100000",success=False)
    cli("lm","generate",language_model,"hi\t","--tokens","2",success=False)
    poisoned_text=root/"malicious.nqlm"
    poisoned_text.write_text("NQLM1 99 2\n",encoding="ascii")
    cli("lm","generate",poisoned_text,"hi",success=False)
    bad_corpus=root/"binary.txt"
    bad_corpus.write_bytes(b"test"*10+b"\x00")
    cli("lm","train",bad_corpus,root/"bad.nqlm",success=False)

print("PASS: typed .noqeri AI modules, SGD classifier, bounded agent and small CPU text-model training")
