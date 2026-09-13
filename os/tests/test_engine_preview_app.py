#!/usr/bin/env python3
"""Build a test-only Qt preload driver and exercise the actual application.
No installed profiles, real music, firmware or physical devices are required.
"""
from pathlib import Path
import tempfile,subprocess,os,wave,json,hashlib,argparse,shlex,sqlite3
parser=argparse.ArgumentParser(description='Linux full-app Engine preview regression with a synthetic library and temporary profile.')
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--reader-prefix',type=Path,required=True)
parser.add_argument('--fixture-generator',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--installed-reader',action='store_true',help='Test normal app-relative reader discovery without an environment override')
args=parser.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
build=args.build.resolve();source=Path(__file__).resolve().parents[2]
helper=args.reader_prefix.resolve()/'libexec/bitedj-engine/engine-import.py'
generator=args.fixture_generator.resolve()
with tempfile.TemporaryDirectory(prefix='bitedj-engine-preview-') as td:
 root=Path(td);profile=root/'profile';profile.mkdir()
 driver=root/'driver.so'
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets'],text=True))
 subprocess.run(['c++','-std=c++20','-fPIC','-shared',str(Path(__file__).with_name('engine_preview_app_driver.cpp')),'-o',str(driver),*flags],check=True)
 (profile/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n')
 drive=root/'drive';drive.mkdir();library=drive/'Engine Library'
 subprocess.run([str(generator),str(library),'initial'],check=True)
 music=drive/'Music';music.mkdir()
 with wave.open(str(music/'Signal Original.wav'),'wb') as f:
  f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100);f.writeframes(bytes(132300*4))
 slow=root/'slow.py';slow.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(30)\n');slow.chmod(0o700)
 before=hashlib.sha256((library/'Database2/m.db').read_bytes()).hexdigest()
 env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',LANG='C.UTF-8',LD_PRELOAD=str(driver),BITEDJ_PREVIEW_FIXTURE=str(drive),BITEDJ_ENGINE_IMPORT_HELPER=str(helper),BITEDJ_PREVIEW_SLOW_HELPER=str(slow),BITEDJ_PREVIEW_IMAGE=str(out/'engine-preview.png'))
 if args.installed_reader: env.pop('BITEDJ_ENGINE_IMPORT_HELPER',None)
 with (out/'engine-preview-app.log').open('w') as log:
  done=subprocess.run([str(build/'mixxx'),'--settings-path',str(profile),'--resource-path',str(source/'res')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65)
 after=hashlib.sha256((library/'Database2/m.db').read_bytes()).hexdigest()
 with sqlite3.connect(profile/'mixxxdb.sqlite') as db:
  importedCount=db.execute('select count(*) from library').fetchone()[0]
 result={'exit':done.returncode,'sourceDatabaseUnchanged':before==after,'importedTrackCount':importedCount,'binarySHA256':hashlib.sha256((build/'mixxx').read_bytes()).hexdigest(),'driverPassed':'PREVIEW_TEST PASS' in (out/'engine-preview-app.log').read_text(errors='replace') and 'PREVIEW_TEST failed' not in (out/'engine-preview-app.log').read_text(errors='replace')}
 (out/'engine-preview-app-results.json').write_text(json.dumps(result,indent=2));print(result)
 assert done.returncode==0 and before==after and importedCount==0 and result['driverPassed']
