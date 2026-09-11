import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from engine_reader_protocol import read_limited,validate_tracks

class ReaderProtocol(unittest.TestCase):
    def sample(self):
        return {'sourceUuid':'fixture-library','tracks':[{
            'id':'1','title':'Fixture','relativePath':'../music/fixture.wav',
            'artist':None,'album':None,'genre':None,'durationMs':3000,
            'bpm':120,'mainCueFrame':100.5,'sampleCount':'132300','sampleRate':44100,
            'hotCues':[{'slot':4,'frame':100.5,'label':'Cue','rgba':[10,20,30,255]}],
            'loops':[{'slot':1,'startFrame':1000.25,'endFrame':2000.75,'label':'Loop','rgba':[30,20,10,255]}],
            'sameSlotCollisions':[],
            'beatgrid':[{'index':-1,'frame':-100.5},{'index':4,'frame':110149.5}]}]}
    def test_separate_banks(self):
        d=self.sample();self.assertEqual(validate_tracks(d,d['sourceUuid']),{'1'})
        d['tracks'][0]['hotCues'][0]['slot']=1
        d['tracks'][0]['sameSlotCollisions']=[1]
        self.assertEqual(validate_tracks(d,d['sourceUuid']),{'1'})
    def test_invalid_tracks(self):
        mutations=[lambda t:t.update(id='-1'),lambda t:t.update(sampleRate=float('nan')),
          lambda t:t.update(relativePath='bad\x00path'),lambda t:t['loops'][0].update(endFrame=0),
          lambda t:t['hotCues'][0].update(slot=9),lambda t:t['hotCues'][0].update(rgba=[-1,0,0,0]),
          lambda t:t['loops'].append(copy.deepcopy(t['loops'][0])),
          lambda t:t['beatgrid'].reverse(),lambda t:t.update(sameSlotCollisions=[1])]
        for mutate in mutations:
            d=self.sample();mutate(d['tracks'][0])
            with self.subTest(mutation=mutate),self.assertRaises(ValueError):validate_tracks(d,d['sourceUuid'])
        d=self.sample();d['tracks'].append(copy.deepcopy(d['tracks'][0]))
        with self.assertRaises(ValueError):validate_tracks(d,d['sourceUuid'])
        with self.assertRaises(ValueError):validate_tracks(self.sample(),'wrong')
    def test_main_cue_state(self):
        d=self.sample()
        for state in ({'defaultFrame':12345.5,'adjustedFrame':0,'isAdjusted':False},
                      {'defaultFrame':0,'adjustedFrame':45678.25,'isAdjusted':True}):
            d['tracks'][0]['mainCueState']=state
            self.assertEqual(validate_tracks(d,d['sourceUuid']),{'1'})
        for state in (None,{}, {'defaultFrame':0,'adjustedFrame':0,'isAdjusted':1},
                      {'defaultFrame':True,'adjustedFrame':0,'isAdjusted':False},
                      {'defaultFrame':0,'adjustedFrame':float('inf'),'isAdjusted':False}):
            d['tracks'][0]['mainCueState']=state
            with self.subTest(state=state),self.assertRaises(ValueError):validate_tracks(d,d['sourceUuid'])
    def test_process_failures(self):
        cases=[("print('{\"a\":1,\"a\":2}')",ValueError,60,1024),
               ("print('NaN')",ValueError,60,1024),
               ("while True: pass",subprocess.TimeoutExpired,0.2,1024),
               ("import os\nwhile True: os.write(1,b'x'*4096)",subprocess.CalledProcessError,5,1024)]
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'reader'
            for script,error,timeout,limit in cases:
                p.write_text('#!/usr/bin/python3\n'+script+'\n');p.chmod(0o700)
                with self.subTest(script=script),self.assertRaises(error):read_limited(p,temp,timeout,limit)

if __name__=='__main__':unittest.main()
