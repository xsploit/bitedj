"""Compile the real Qt parent validator; exercise protocol and graph failures."""
import argparse,copy,json,subprocess,shlex,tempfile
from pathlib import Path
from native_test_support import fpclassify_object
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--fixture',type=Path);args=p.parse_args()
base={'protocol':'bitedj.engine.import','protocolVersion':1,'schema':'3.0.2','sourceUuid':'fixture',
 'frameUnit':'audio frames at track sample rate','mediaPathContext':{'libraryDirectory':'/media/fixture/Engine Library','relativePathBase':'original Engine Library directory'},
 'tracks':[{'id':'1','title':'Track','relativePath':'../music/a.wav','artist':None,'album':None,'genre':None,'bpm':120,'durationMs':3000,'mainCueFrame':0,'sampleCount':'132300','sampleRate':44100,
 'hotCues':[{'slot':1,'frame':100.5,'label':'Cue','rgba':[1,2,3,255]}],
 'loops':[{'slot':1,'startFrame':1000.25,'endFrame':2000.75,'label':'Loop','rgba':[3,2,1,255]}],
 'sameSlotCollisions':[1],'beatgrid':[{'index':-1,'frame':-500.5},{'index':4,'frame':110000.25}]}],
 'playlists':[{'id':'1','parentId':None,'position':0,'title':'Parent','tracks':[{'entryId':'1','trackId':'1','sourceUuid':'fixture','resolution':'local'},{'entryId':'2','trackId':'1','sourceUuid':'fixture','resolution':'local'}]},
 {'id':'2','parentId':'1','position':0,'title':'Child','tracks':[{'entryId':'3','trackId':'9','sourceUuid':'other','resolution':'unresolved'}]}]}
code='''#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <iostream>
#include "library/engine/engineimportpackage.h"
int main(int argc,char**argv){QCoreApplication app(argc,argv);QFile in;if(!in.open(stdin,QIODevice::ReadOnly))return 2;QJsonParseError parse;auto doc=QJsonDocument::fromJson(in.readAll(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject())return 2;QString error;bool ok=mixxx::validateEngineImportPackage(doc.object(),&error);if(!ok)std::cerr<<error.toStdString();return ok?0:1;}
'''
mutations=[
 lambda x:x.update(protocolVersion=True),lambda x:x.update(schema='9.0.0'),lambda x:x.update(sourceUuid=''),lambda x:x.update(frameUnit='samples'),
 lambda x:x.pop('mediaPathContext'),lambda x:x.update(tracks={}),lambda x:x['tracks'].append(copy.deepcopy(x['tracks'][0])),
 lambda x:x['tracks'][0].update(id='01'),lambda x:x['tracks'][0].update(sampleCount='18446744073709551616'),
 lambda x:x['tracks'][0].update(sampleRate=None),lambda x:x['tracks'][0].pop('genre'),
 lambda x:x['tracks'][0]['hotCues'].append(copy.deepcopy(x['tracks'][0]['hotCues'][0])),
 lambda x:x['tracks'][0]['hotCues'][0].update(frame=-2),lambda x:x['tracks'][0]['loops'][0].update(startFrame=-1),
 lambda x:x['tracks'][0]['loops'][0].update(endFrame=0),lambda x:x['tracks'][0]['hotCues'][0].update(slot=True),
 lambda x:x['tracks'][0]['hotCues'][0].update(rgba=[1,2,3,256]),lambda x:x['tracks'][0].update(sameSlotCollisions=[]),
 lambda x:x['tracks'][0]['beatgrid'].reverse(),lambda x:x['tracks'][0]['beatgrid'][0].update(index=0.5),
 lambda x:x['playlists'][1].update(parentId='99'),lambda x:x['playlists'][0].update(parentId='2'),
 lambda x:x['playlists'][1].update(id='1'),lambda x:x['playlists'][1].update(position=1),
 lambda x:x['playlists'][1]['tracks'][0].update(entryId='1'),lambda x:x['playlists'][1]['tracks'][0].update(resolution='local'),
 lambda x:x['playlists'][0]['tracks'][0].update(resolution='unresolved'),lambda x:x['playlists'][0].update(title='bad\x00name')]
for field, invalid in [('keyId',24),('keyId',True),('bitrateKbps',-1),('ratingPercent',101),('year',2**31),('trackNumber',1.5),('fileBytes','01'),('fileBytes',str(2**64)),('fileBytes',42),('sourceTitle',False),('comment','bad\x00text'),('composer',False),('publisher','x'*65537)]:
 mutations.append(lambda x,k=field,v=invalid:x['tracks'][0].update({k:v}))
with tempfile.TemporaryDirectory() as temp:
 d=Path(temp);(d/'main.cpp').write_text(code);exe=d/'test'
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core'],text=True))
 subprocess.run(['c++','-std=c++20','-O3','-ffast-math','-fPIC','-Wall','-Wextra','-Werror',str(d/'main.cpp'),str(root/'src/library/engine/engineimportpackage.cpp'),fpclassify_object(d),'-I'+str(root/'src'),*flags,'-o',str(exe)],check=True)
 def check(value,valid):
  result=subprocess.run([str(exe)],input=json.dumps(value),text=True,capture_output=True,timeout=10)
  assert result.returncode==(0 if valid else 1),(result.returncode,result.stderr)
 check(base,True)
 for key_id in range(24):
  extended=copy.deepcopy(base);extended['tracks'][0].update(sourceTitle=None,keyId=key_id,comment='',composer=None,publisher='Publisher',bitrateKbps=1536,ratingPercent=60,year=2026,trackNumber=1,fileBytes='9007199254740993');check(extended,True)
 empty=copy.deepcopy(base);empty.update(tracks=[],playlists=[]);check(empty,True)
 for mutate in mutations:
  value=copy.deepcopy(base);mutate(value);check(value,False)
 if args.fixture:check(json.loads(args.fixture.read_text()),True)
 print(f'PASS: {26+len(mutations)+bool(args.fixture)} parent validation cases; same-slot banks, fractional/negative grid, duplicates and unresolved refs preserved')
