"""Parent-side validation and Linux process limits for the Engine reader prototype."""
import json
import math
from pathlib import Path
import resource
import subprocess
import tempfile


def read_limited(reader, library, timeout=60, max_output=64*1024*1024):
    def limits():
        resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
        resource.setrlimit(resource.RLIMIT_CPU,(60,60))
        resource.setrlimit(resource.RLIMIT_FSIZE,(max_output,max_output))
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        subprocess.run([str(Path(reader).resolve()),str(library)],stdout=out,stderr=err,
                       timeout=timeout,check=True,preexec_fn=limits)
        if out.tell()>max_output or err.tell()>max_output:
            raise ValueError('Reader output exceeded limit')
        out.seek(0)
        def invalid_constant(value):
            raise ValueError('Non-finite JSON constant: '+value)
        def unique_object(pairs):
            result={}
            for k,v in pairs:
                if k in result: raise ValueError('Duplicate JSON key')
                result[k]=v
            return result
        return json.loads(out.read(max_output+1),parse_constant=invalid_constant,
                          object_pairs_hook=unique_object)


def validate_tracks(data, source_uuid):
    def require(ok, reason):
        if not ok: raise ValueError(reason)
    def number(v):
        return type(v) in (int,float) and math.isfinite(v)
    def text(v):
        return isinstance(v,str) and len(v)<=65536 and '\x00' not in v
    def identity(v):
        return isinstance(v,str) and v.isascii() and v.isdecimal() and 0<int(v)<2**63 and str(int(v))==v
    require(isinstance(data,dict),'Reader result must be an object')
    require(data.get('sourceUuid')==source_uuid,'Reader source identity mismatch')
    tracks=data.get('tracks')
    require(isinstance(tracks,list) and len(tracks)<=1000000,'Invalid track collection')
    ids=set()
    for t in tracks:
        require(isinstance(t,dict),'Invalid track object')
        ident=t.get('id')
        require(identity(ident) and ident not in ids,'Invalid or duplicate track ID'); ids.add(ident)
        for key in ('title','relativePath'):
            require(text(t.get(key)),'Invalid '+key)
        for key in ('artist','album','genre'):
            require(key in t and (t[key] is None or text(t[key])),'Invalid '+key)
        for key in ('sourceTitle','comment','composer','publisher'):
            if key in t:
                require(t[key] is None or text(t[key]),'Invalid '+key)
        for key,low,high in (('keyId',0,23),('bitrateKbps',0,2147483647),('ratingPercent',0,100),('year',-2147483648,2147483647),('trackNumber',-2147483648,2147483647)):
            if key in t:
                require(t[key] is None or (type(t[key]) is int and low<=t[key]<=high),'Invalid '+key)
        if 'fileBytes' in t:
            value=t['fileBytes']
            require(value is None or (isinstance(value,str) and value.isascii() and value.isdecimal() and 0<=int(value)<2**64 and str(int(value))==value),'Invalid fileBytes')
        for key in ('durationMs','bpm','mainCueFrame'):
            require(key in t and (t[key] is None or number(t[key])),'Invalid '+key)
        count=t.get('sampleCount')
        require('sampleCount' in t and (count is None or (isinstance(count,str) and count.isascii() and count.isdecimal() and 0<=int(count)<2**64 and str(int(count))==count)), 'Invalid sample count')
        require(number(t.get('sampleRate')) and t['sampleRate']>=0,'Invalid sample rate')
        slots={}
        for bank in ('hotCues','loops'):
            entries=t.get(bank)
            require(isinstance(entries,list) and len(entries)<=8,'Invalid cue bank')
            slots[bank]=set()
            for cue in entries:
                require(isinstance(cue,dict),'Invalid cue')
                slot=cue.get('slot')
                require(type(slot) is int and 1<=slot<=8 and slot not in slots[bank],'Invalid cue slot')
                slots[bank].add(slot)
                require(text(cue.get('label')),'Invalid cue label')
                rgba=cue.get('rgba')
                require(isinstance(rgba,list) and len(rgba)==4 and all(type(x) is int and 0<=x<=255 for x in rgba),'Invalid cue color')
                if bank=='loops':
                    require(number(cue.get('startFrame')) and number(cue.get('endFrame')) and cue['endFrame']>cue['startFrame'],'Invalid loop bounds')
                else: require(number(cue.get('frame')),'Invalid cue frame')
        require(t.get('sameSlotCollisions')==sorted(slots['hotCues'] & slots['loops']),'Incorrect cue collision report')
        grid=t.get('beatgrid')
        require(isinstance(grid,list) and len(grid)<=1000000,'Invalid beat grid')
        previous=None
        for beat in grid:
            require(isinstance(beat,dict) and number(beat.get('index')) and number(beat.get('frame')),'Invalid beat')
            if previous is not None:
                require(beat['index']>previous['index'] and beat['frame']>previous['frame'],'Non-monotonic beat grid')
            previous=beat
    return ids
