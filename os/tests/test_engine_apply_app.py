#!/usr/bin/env python3
"""Full-app Engine metadata/playlist Apply acceptance using synthetic media only."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,sqlite3,subprocess,tempfile,wave

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--reader-prefix',type=Path,required=True)
p.add_argument('--fixture-generator',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
build=a.build.resolve(); source=Path(__file__).resolve().parents[2]
helper=a.reader_prefix.resolve()/'libexec/bitedj-engine/engine-import.py'
generator=a.fixture_generator.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
results={}
with tempfile.TemporaryDirectory(prefix='bitedj-engine-apply-') as temporary:
 root=Path(temporary);driver=root/'driver.so'
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets','Qt6Sql'],text=True))
 subprocess.run(['c++','-std=c++20','-fPIC','-shared',str(Path(__file__).with_name('engine_apply_app_driver.cpp')),'-o',str(driver),*flags],check=True)
 drive=root/'drive';drive.mkdir();library=drive/'Engine Library'
 subprocess.run([str(generator),str(library),'initial'],check=True)
 music=drive/'Music';music.mkdir()
 for name in ('Signal Original.wav','Signal VIP.wav'):
  with wave.open(str(music/name),'wb') as f:
   f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100);f.writeframes(bytes(132300*4))
 engine=library/'Database2/m.db'
 def profile(name):
  path=root/name;path.mkdir();(path/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n');return path
 def run(name,settings,**extra):
  before=hashlib.sha256(engine.read_bytes()).hexdigest()
  media_hash={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in music.iterdir()}
  env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',LANG='C.UTF-8',LD_PRELOAD=str(driver),BITEDJ_PREVIEW_FIXTURE=str(drive),BITEDJ_ENGINE_IMPORT_HELPER=str(helper),BITEDJ_APPLY_DATABASE=str(settings/'mixxxdb.sqlite'),BITEDJ_APPLY_IMAGE=str(out/f'engine-apply-{name}.png'),**extra)
  log=out/f'engine-apply-{name}.log'
  with log.open('w') as output:
   done=subprocess.run([str(build/'mixxx'),'--settings-path',str(settings),'--resource-path',str(source/'res')],env=env,stdout=output,stderr=subprocess.STDOUT,timeout=65)
  text=log.read_text(errors='replace')
  assert done.returncode==0 and 'APPLY_TEST PASS' in text and 'APPLY_TEST failed' not in text,(name,done.returncode,text[-3000:])
  assert before==hashlib.sha256(engine.read_bytes()).hexdigest(), 'source DB mutated'
  assert media_hash=={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in music.iterdir()},'media mutated with default metadata-sync settings'
  with sqlite3.connect(settings/'mixxxdb.sqlite') as db:
   state={'tracks':db.execute('SELECT id,title,comment,rating FROM library ORDER BY id').fetchall(),
     'playlists':db.execute('SELECT id,name FROM Playlists WHERE hidden=0 ORDER BY id').fetchall(),
     'entries':db.execute('SELECT id,playlist_id,track_id,position FROM PlaylistTracks WHERE playlist_id IN (SELECT id FROM Playlists WHERE hidden=0) ORDER BY playlist_id,position,id').fetchall(),
     'origins':db.execute('SELECT entity_kind,source_id,local_id,source_baseline FROM engine_import_entities ORDER BY entity_kind,source_id').fetchall(),
     'engineCues':db.execute('SELECT COUNT(*) FROM cues WHERE engine_library_uuid IS NOT NULL').fetchone()[0],
     'schema':db.execute("SELECT value FROM settings WHERE name='mixxx.schema.version'").fetchone()[0],
     'integrity':db.execute('PRAGMA integrity_check').fetchone()[0]}
   state['origins']=[(kind,sid,lid,json.loads(baseline)) for kind,sid,lid,baseline in state['origins']]
  assert state['engineCues']==0 and state['integrity']=='ok'
  results[name]={'exit':done.returncode,'sourceUnchanged':True,'mediaUnchanged':True,'state':state}
  (out/'engine-apply-results.json').write_text(json.dumps(results,indent=2))
  print('PASS',name,len(state['tracks']),'tracks',len(state['playlists']),'playlists',flush=True)
  return state,text
 settings=profile('profile')
 first,text=run('first',settings,BITEDJ_APPLY_EDIT_LOCAL='1')
 assert len(first['tracks'])==2 and len(first['playlists'])==2 and len(first['entries'])==3
 assert first['tracks'][0][1]=='My local title' and first['tracks'][1][1]=='Signal (VIP)'
 assert [x[1] for x in first['playlists']]==['Prepared set','Prepared set / Versions']
 root_entries=[x for x in first['entries'] if x[1]==first['playlists'][0][0]]
 assert [x[2] for x in root_entries]==[first['tracks'][0][0],first['tracks'][1][0]]
 # A local duplicate is allowed in BiteDJ even though this Engine schema has
 # a per-playlist UNIQUE track constraint. Simulate this between app runs.
 with sqlite3.connect(settings/'mixxxdb.sqlite') as db:
  db.execute('INSERT INTO PlaylistTracks(playlist_id,track_id,position) VALUES(?,?,3)',(first['playlists'][0][0],first['tracks'][0][0]))
  expected_entries=db.execute('SELECT id,playlist_id,track_id,position FROM PlaylistTracks WHERE playlist_id IN (SELECT id FROM Playlists WHERE hidden=0) ORDER BY playlist_id,position,id').fetchall()
 repeat,text=run('repeat',settings)
 assert repeat['entries']==expected_entries,'local duplicate was removed or reordered'
 assert all(repeat[key]==first[key] for key in first if key!='entries'),'repeat import changed identities, metadata or accepted baselines'
 subprocess.run([str(generator),str(library),'update'],check=True)
 update,text=run('source-update',settings)
 assert update['tracks'][0][1:]==('My local title','Source comment B',5)
 assert update['entries']==expected_entries and 'Kept local edits: title' in text
 assert any(k=='track' and sid=='1' and base['title']=='Signal (Original)' and 'deferredTiming' in base for k,sid,lid,base in update['origins'])
 cancelled,text=run('cancel',profile('cancel-profile'),BITEDJ_APPLY_CANCEL='1')
 assert len(cancelled['tracks'])==1 and not cancelled['playlists'] and 'Cancelled' in text
 (music/'Signal VIP.wav').rename(root/'missing-vip.wav')
 missing,text=run('missing',profile('missing-profile'))
 assert len(missing['tracks'])==1 and not missing['playlists'] and 'No entries were omitted' in text
 (root/'missing-vip.wav').rename(music/'Signal VIP.wav')
 failed,text=run('failed-save',profile('failed-profile'),BITEDJ_APPLY_FAIL_SAVE='1')
 assert not failed['origins'] and not failed['playlists'] and 'could not be saved' in text
 collision_profile=profile('collision-profile')
 collision,text=run('collision',collision_profile,BITEDJ_APPLY_NAME_COLLISION='1')
 assert [x[1] for x in collision['playlists']]==['Prepared set','Prepared set (2)','Prepared set / Versions']
 collision_repeat,text=run('collision-repeat',collision_profile)
 assert collision_repeat==collision,'name collision caused duplicate/reassigned playlists'
 with sqlite3.connect(engine) as db:
  db.execute("UPDATE Playlist SET title='Renamed set' WHERE parentListId=0")
 renamed,text=run('source-rename',collision_profile)
 assert [x[1] for x in renamed['playlists']]==['Prepared set','Renamed set','Renamed set / Versions']
 assert renamed['entries']==collision['entries'] and 'Kept local playlist edits' not in text
 results['binarySHA256']=hashlib.sha256((build/'mixxx').read_bytes()).hexdigest()
 (out/'engine-apply-results.json').write_text(json.dumps(results,indent=2))
print('PASS full-app Apply acceptance',flush=True)
