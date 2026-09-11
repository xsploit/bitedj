#!/usr/bin/env python3
"""Exercise native CueData reset and blob decoding under PC ARM emulation.

Requires the user's preserved runtime, Zig and user/network namespace support.
No Pi connection, hardware access, application main or audio decoder call is made.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

EXPECTED='76fd56d8906a9818d3de3cdad03dc37d0eb1594fa910ec3ec3796f63b673e888'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runtime',type=Path,required=True)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--qemu',type=Path,default=Path('/usr/bin/qemu-aarch64-static'))
p.add_argument('--result',type=Path,required=True)
a=p.parse_args()
runtime=a.runtime.resolve(); binary=runtime/'usr/Engine/Engine'
actual=hashlib.sha256(binary.read_bytes()).hexdigest()
if actual!=EXPECTED:
 p.error('unsupported executable hash: the probe offsets must not be used on another binary')
source=Path(__file__).with_name('cue-reset-probe.c').resolve()
with tempfile.TemporaryDirectory(prefix='engine-cue-reset-') as temporary:
 shim=Path(temporary)/'cue-reset.so'
 subprocess.run([str(a.zig.resolve()),'cc','-target','aarch64-linux-gnu.2.35',
  '-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',str(source),'-ldl','-o',str(shim)],check=True,timeout=120)
 run=subprocess.run(['unshare','-Urn',str(a.qemu.resolve()),str(runtime/'lib/ld-linux-aarch64.so.1'),
  '--library-path',str(runtime/'usr/lib')+':'+str(runtime/'lib'),
  '--preload',str(shim),str(binary)],capture_output=True,text=True,timeout=20)
 after=hashlib.sha256(binary.read_bytes()).hexdigest()
 result={'scope':'Native CueData constructor, reset and synthetic decompressed blob decoding; no SQLite load, audio decoder, Engine main or Pi execution',
  'engineSHA256':actual,'probeSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),
  'runtimeUnchanged':after==actual,'engineSHA256After':after,
  'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,
  'passed':after==actual and run.returncode==0 and 'CUE_PROBE PASS' in run.stdout}
 a.result.write_text(json.dumps(result,indent=2)+'\n')
 print(run.stdout,end='');print(run.stderr,end='')
 raise SystemExit(0 if result['passed'] else 1)
