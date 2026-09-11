from pathlib import Path
import argparse,tempfile,subprocess,os,shlex,wave,sqlite3,json,hashlib
r=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description='Synthetic full-app recursive scan and rollback acceptance')
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();b=a.build.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=r.parents[1]
with tempfile.TemporaryDirectory(prefix='bitedj-scan-check-') as td:
 root=Path(td);driver=root/'driver.so'
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets','Qt6Sql'],text=True))
 subprocess.run(['c++','-std=c++20','-shared','-fPIC',str(r/'scanner_app_driver.cpp'),'-o',str(driver),*flags],check=True)
 music=root/'Music';(music/'nested').mkdir(parents=True)
 for name in ['one.wav','nested/two.wav','nested/three.wav']:
  with wave.open(str(music/name),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100);f.writeframes(bytes(44100*4))
 hashes={str(p.relative_to(music)):hashlib.sha256(p.read_bytes()).hexdigest() for p in music.rglob('*.wav')}
 results={}
 for mode in ['normal','rejected']:
  profile=root/mode;profile.mkdir();(profile/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n')
  env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',LANG='C.UTF-8',LD_PRELOAD=str(driver),SCAN_DB=str(profile/'mixxxdb.sqlite'),SCAN_MUSIC=str(music))
  if mode=='rejected':env['SCAN_REJECT']='1'
  log=out/f'scan-{mode}.log'
  with log.open('w') as f:done=subprocess.run([str(b/'mixxx'),'--settings-path',str(profile),'--resource-path',str(source/'res')],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=55)
  text=log.read_text(errors='replace');assert done.returncode==0 and 'SCAN_TEST PASS' in text,(mode,done.returncode,text[-2500:])
  with sqlite3.connect(profile/'mixxxdb.sqlite') as db:
   count=db.execute('select count(*) from library').fetchone()[0];integrity=db.execute('pragma integrity_check').fetchone()[0]
  assert count==(3 if mode=='normal' else 0) and integrity=='ok'
  assert hashes=={str(p.relative_to(music)):hashlib.sha256(p.read_bytes()).hexdigest() for p in music.rglob('*.wav')}
  results[mode]={'tracks':count,'integrity':integrity,'exit':done.returncode,'mediaUnchanged':True}
  (out/'scan-results.json').write_text(json.dumps(results,indent=2));print('PASS',mode,results[mode],flush=True)
