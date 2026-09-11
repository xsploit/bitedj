"""Compile/run production asynchronous reader service with controlled executables."""
import json,os,shlex,subprocess,tempfile,time
from pathlib import Path
from native_test_support import fpclassify_object
from engine_reader_json_cases import cases as raw_cases
root=Path(__file__).resolve().parents[2]
base={'protocol':'bitedj.engine.import','protocolVersion':1,'schema':'3.0.2','sourceUuid':'fixture','frameUnit':'audio frames at track sample rate','mediaPathContext':{'libraryDirectory':'/fixture/Engine Library','relativePathBase':'original Engine Library directory'},'tracks':[],'playlists':[]}
with tempfile.TemporaryDirectory() as temp:
 d=Path(temp);exe=d/'probe';moc=d/'moc_enginereaderservice.cpp'
 subprocess.run(['/usr/lib/qt6/moc',str(root/'src/library/engine/enginereaderservice.h'),'-o',str(moc)],check=True)
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core'],text=True))
 sources=[root/'os/tests/engine_reader_service_probe.cpp',root/'src/library/engine/enginereaderservice.cpp',root/'src/library/engine/engineimportpackage.cpp']
 subprocess.run(['c++','-std=c++20','-O3','-ffast-math','-fPIC','-Wall','-Wextra','-Werror',*[str(s) for s in sources],fpclassify_object(d),'-I'+str(root/'src'),'-I'+str(d),*flags,'-o',str(exe)],check=True)
 encoded=json.dumps(base)
 cases=[('valid',f'time.sleep(.2);print({encoded!r})','ready'),
 ('repeat',f'time.sleep(.2);print({encoded!r})','ready-twice'),
 ('queued-cancel',f'print({encoded!r})','cancel-queued'),
 ('invalid-json','print("{")','failed: Invalid JSON'),
 ('duplicate',f'print({("{\"sourceUuid\":\"other\","+encoded[1:])!r})','failed: Duplicate'),
 ('escaped-duplicate',f'print({("{\"source\\u0055uid\":\"other\","+encoded[1:])!r})','failed: Duplicate'),
 ('exit-code',f'print({encoded!r});sys.exit(2)','failed: Engine reader failed'),
 ('schema',f'print({encoded.replace("3.0.2","9.0.0")!r})','failed:'),
 ('oversize','sys.stdout.write("x"*(64*1024*1024+1))','failed: Engine reader output exceeds'),
 ('stderr','sys.stderr.write("x"*(1024*1024+1))','failed: Engine reader diagnostics exceed'),
 ('cancel','time.sleep(30)','cancelled')]
 for name,body,expected in cases:
  helper=d/name;helper.write_text('#!/usr/bin/env python3\nimport sys,time\n'+body+'\n');helper.chmod(0o700)
  subprocess.run([str(exe),str(helper),str(d),expected],check=True,timeout=12)
 for name,payload,valid in raw_cases():
  helper=d/('raw-'+name);helper.write_text('#!/usr/bin/env python3\nimport sys,time\ntime.sleep(.1)\n'+f'sys.stdout.buffer.write({payload!r})\n');helper.chmod(0o700)
  result=subprocess.run([str(exe),str(helper),str(d),'ready' if valid else 'failed:'],capture_output=True,text=True,timeout=12)
  assert result.returncode==0,(name,result.stdout,result.stderr)
 # Cancellation must also kill a descendant whose parent exits on SIGTERM.
 pidfile=d/'child.pid';helper=d/'child-helper'
 helper.write_text('#!/usr/bin/env python3\nimport subprocess,time\nfrom pathlib import Path\n'+f'p=subprocess.Popen(["sleep","30"]);Path({str(pidfile)!r}).write_text(str(p.pid));time.sleep(30)\n');helper.chmod(0o700)
 try:
  subprocess.run([str(exe),str(helper),str(d),'cancelled'],check=True,timeout=12)
  pid=int(pidfile.read_text());time.sleep(.1)
  stat=Path(f'/proc/{pid}/stat')
  assert not stat.exists() or stat.read_text().split(') ',1)[1][0]=='Z','Reader descendant survived cancellation'
 finally:
  if pidfile.exists():
   try:os.kill(int(pidfile.read_text()),9)
   except ProcessLookupError:pass
 # A crashed/exited launcher can leave its child running after its own PID is gone.
 helper.write_text('#!/usr/bin/env python3\nimport subprocess,sys\nfrom pathlib import Path\n'+f'p=subprocess.Popen(["sleep","30"]);Path({str(pidfile)!r}).write_text(str(p.pid));sys.exit(2)\n')
 try:
  subprocess.run([str(exe),str(helper),str(d),'failed: Engine reader failed'],check=True,timeout=12)
  pid=int(pidfile.read_text());time.sleep(.1);stat=Path(f'/proc/{pid}/stat')
  assert not stat.exists() or stat.read_text().split(') ',1)[1][0]=='Z','Reader descendant survived launcher exit'
 finally:
  if pidfile.exists():
   try:os.kill(int(pidfile.read_text()),9)
   except ProcessLookupError:pass
 print('PASS: async GUI heartbeat, single terminal result, busy rejection, strict JSON, limits, failure and cancellation including descendant')
