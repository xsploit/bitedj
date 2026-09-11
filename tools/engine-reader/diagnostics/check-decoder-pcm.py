#!/usr/bin/env python3
"""Exercise the preserved Engine file factory and reader under PC ARM emulation.

Produces sequential and repeated-seek PCM for comparison. No application main
or Pi run; requires a preserved runtime, Zig, and Linux user/network namespaces.
This runner reports harness execution, not cross-decoder alignment success.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import os

EXPECTED='76fd56d8906a9818d3de3cdad03dc37d0eb1594fa910ec3ec3796f63b673e888'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runtime',type=Path,required=True)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--qemu',type=Path,default=Path('/usr/bin/qemu-aarch64-static'))
p.add_argument('--result',type=Path,required=True)
p.add_argument('--input',type=Path,required=True)
p.add_argument('--pcm-output',type=Path,required=True)
a=p.parse_args()
runtime=a.runtime.resolve(); binary=runtime/'usr/Engine/Engine'
actual=hashlib.sha256(binary.read_bytes()).hexdigest()
input_before=hashlib.sha256(a.input.read_bytes()).hexdigest()
if actual!=EXPECTED:
 p.error('unsupported executable hash: the probe offsets must not be used on another binary')
source=Path(__file__).with_name('decoder-pcm-probe.c').resolve()
with tempfile.TemporaryDirectory(prefix='engine-decoder-entry-') as temporary:
 shim=Path(temporary)/'entry.so'
 subprocess.run([str(a.zig.resolve()),'cc','-target','aarch64-linux-gnu.2.35',
  '-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',str(source),'-ldl','-o',str(shim)],check=True,timeout=120)
 env=os.environ.copy();env['ENGINE_READER_INPUT']=str(a.input.resolve())
 if a.pcm_output:env['ENGINE_READER_OUTPUT']=str(a.pcm_output.resolve())
 run=subprocess.run(['unshare','-Urn',str(a.qemu.resolve()),str(runtime/'lib/ld-linux-aarch64.so.1'),
  '--library-path',str(runtime/'usr/lib')+':'+str(runtime/'lib'),
  '--preload',str(shim),str(binary)],capture_output=True,text=True,timeout=20,env=env)
 result={'scope':'Native Engine file factory, decoder, sequential PCM and seeks under PC emulation; no application main or Pi',
  'engineSHA256':actual,'inputSHA256':input_before,'inputUnchanged':input_before==hashlib.sha256(a.input.read_bytes()).hexdigest(),'probeSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),
  'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,
  'passed':run.returncode==0 and 'PCM_PROBE PASS' in run.stdout}
 result['passed']=result['passed'] and result['inputUnchanged']
 a.result.write_text(json.dumps(result,indent=2)+'\n')
 print(run.stdout[-2000:],end='');print(run.stderr[-3500:],end='')
 raise SystemExit(0 if result['passed'] else 1)
