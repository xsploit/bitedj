"""Create playable synthetic initial/update Engine libraries for app import tests."""
from pathlib import Path
import argparse,array,hashlib,json,math,shutil,subprocess,sys,wave,sqlite3
p=argparse.ArgumentParser(description=__doc__);p.add_argument('generator',type=Path);p.add_argument('launcher',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
r=a.output.resolve();r.mkdir()  # Refuse to overwrite any existing fixture/profile.
generator=a.generator.resolve();launcher=a.launcher.resolve()
initial=r/'initial';music=initial/'Music';music.mkdir(parents=True)
for name,hz in [('Original',220),('VIP',330)]:
 samples=array.array('h')
 for frame in range(132300):
  envelope=min(1,frame/4410,(132299-frame)/4410)
  value=round(1000*envelope*math.sin(2*math.pi*hz*frame/44100))
  samples.extend([value,value])
 if sys.byteorder!='little':samples.byteswap()
 with wave.open(str(music/f'Signal {name}.wav'),'wb') as out:
  out.setparams((2,2,44100,132300,'NONE','not compressed'));out.writeframes(samples.tobytes())
subprocess.run([str(generator),str(initial/'Engine Library'),'initial'],check=True)
updated=r/'updated';shutil.copytree(initial,updated)
subprocess.run([str(generator),str(updated/'Engine Library'),'update'],check=True)
packages={}
for label,folder in [('initial',initial),('updated',updated)]:
 packages[label]=json.loads(subprocess.check_output([str(launcher),str(folder/'Engine Library'),'--media-root',str(folder)],timeout=60))
 (r/(label+'.json')).write_text(json.dumps(packages[label],indent=2)+'\n')
for data in packages.values():
 assert len(data['tracks'])==2 and len(data['playlists'])==2
 assert [entry['trackId'] for entry in data['playlists'][0]['tracks']]==['1','2']
 for track in data['tracks']:
  assert track['media']['status']=='resolved',track['media']
  assert track['sameSlotCollisions']==[1]
  assert track['sampleCount']=='132300' and track['fileBytes']=='529244'
assert packages['initial']['sourceUuid']==packages['updated']['sourceUuid']
first={t['id']:t for t in packages['initial']['tracks']};second={t['id']:t for t in packages['updated']['tracks']}
assert first['1']['comment']=='Source comment A' and second['1']['comment']=='Source comment B'
assert first['1']['sourceTitle']!=second['1']['sourceTitle']
assert first['1']['hotCues'][0]['frame']==22050.5 and second['1']['hotCues'][0]['frame']==33075.5
assert second['1']['keyId']==0 and second['1']['ratingPercent']==100
assert {k:v for k,v in first['2'].items() if k!='media'}=={k:v for k,v in second['2'].items() if k!='media'},'Unchanged VIP metadata changed'
for name in ('Original','VIP'):
 left=initial/'Music'/f'Signal {name}.wav';right=updated/'Music'/left.name
 assert left.stat().st_size==529244 and left.read_bytes()==right.read_bytes()
 with wave.open(str(left),'rb') as audio:
  assert (audio.getnchannels(),audio.getsampwidth(),audio.getframerate(),audio.getnframes())==(2,2,44100,132300)
  assert len(audio.readframes(132301))==529200
manifest={'sourceUuid':packages['initial']['sourceUuid'],'trackIds':['1','2'],'rootPlaylistMembership':['1','2'],'sampleRate':44100,'audioFrames':132300,'audioChannels':2,'initialCueFrame':22050.5,'updatedCueFrame':33075.5,'localEditScenario':{'id':'1','comment':'My local comment','expectedAfterSourceUpdate':'My local comment','expectedConflictField':'comment'},'audioSha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in music.glob('*.wav')},'verifiedScope':'Reader fixture preparation, not app import'}
(r/'fixture-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
# The generated schema enforces unique track membership. Keep repeat-membership
# coverage as a protocol fixture, not a falsely labelled native database.
import copy
repeated=copy.deepcopy(packages['initial'])
entry=copy.deepcopy(repeated['playlists'][0]['tracks'][0]);entry['entryId']='4'
repeated['playlists'][0]['tracks'].append(entry)
(r/'repeated-membership-protocol-only.json').write_text(json.dumps(repeated,indent=2)+'\n')
print('PASS: playable Original/VIP initial/update libraries, stable identities, separate repeat-membership protocol case, cue/loop banks and source edits')
