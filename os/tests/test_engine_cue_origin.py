#!/usr/bin/env python3
"""Build/run real CueDAO persistence regression against an existing Linux Ninja build.

No installed profiles are read or written. The C++ probe creates synthetic cues
in a QTemporaryDir SQLite database. Requires compile_commands.json and a built
mixxx target. --fresh-cues is an explicit source-object diagnostic mode only.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def compile_command(entry, source, target):
    args=list(entry.get('arguments') or shlex.split(entry['command']))
    result=[]
    i=0
    while i<len(args):
        if args[i] in ('-c','-o'):
            i+=2
            continue
        # Do not overwrite build-system dependency files.
        if args[i] in ('-MF','-MT','-MQ'):
            i+=2
            continue
        result.append(args[i])
        i+=1
    return result+['-c',str(source),'-o',str(target)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir',type=Path)
    parser.add_argument('--probe-arg',action='append',default=[],help='Argument passed to the C++ probe')
    parser.add_argument('--fresh-trackdao',action='store_true',help='Compile current TrackDAO separately for staged-write diagnostics')
    parser.add_argument('--probe-source',type=Path,help='Alternative local C++ persistence/planning probe')
    parser.add_argument('--ninja',default='ninja',help='Ninja executable path')
    parser.add_argument('--fresh-cues',action='store_true',help='Compile current Cue/CueDAO sources separately; not a full-build validation')
    args=parser.parse_args()
    build=args.build_dir.resolve()
    entries=json.loads((build/'compile_commands.json').read_text())
    dao=next(e for e in entries if Path(e['file']).as_posix().endswith('/src/library/dao/cuedao.cpp'))
    commands=subprocess.check_output([args.ninja,'-C',str(build),'-t','commands','mixxx'],text=True)
    candidates=[]
    for line in commands.splitlines():
        tokens=shlex.split(line)
        if '-o' in tokens and tokens[tokens.index('-o')+1]=='mixxx':candidates.append(tokens)
    if len(candidates)!=1:raise RuntimeError('Expected one mixxx link command in Ninja build')
    link=candidates[0]
    start=next(i for i,v in enumerate(link) if Path(v).name in ('c++','g++','clang++'))
    link=link[start:]
    if '&&' in link:link=link[:link.index('&&')]
    with tempfile.TemporaryDirectory(prefix='bitedj-cue-origin-test-') as temp:
        work=Path(temp);obj=work/'probe.o';exe=work/'probe'
        source=args.probe_source.resolve() if args.probe_source else Path(__file__).with_name('engine_cue_origin_probe.cpp')
        subprocess.run(compile_command(dao,source,obj),cwd=build,check=True)
        objects=[obj]
        fresh_sources=[]
        if args.fresh_cues:fresh_sources+=['/src/track/cue.cpp','/src/library/dao/cuedao.cpp']
        if args.fresh_trackdao:fresh_sources+=['/src/library/dao/trackdao.cpp']
        if fresh_sources:
            for suffix in fresh_sources:
                entry=next(e for e in entries if Path(e['file']).as_posix().endswith(suffix))
                target=work/(Path(suffix).stem+'.o')
                subprocess.run(compile_command(entry,entry['file'],target),cwd=build,check=True)
                objects.append(target)
        output=[];i=0
        while i<len(link):
            if link[i]=='-o':output+=['-o',str(exe)];i+=2;continue
            if link[i].endswith('.o'):i+=1;continue
            output.append(link[i]);i+=1
        output[1:1]=[str(p) for p in objects]
        subprocess.run(output,cwd=build,check=True)
        subprocess.run([str(exe),*args.probe_arg],cwd=work,check=True,timeout=30)
    print('Mode: '+('fresh production source objects' if (args.fresh_cues or args.fresh_trackdao) else 'completed application archive'))

if __name__=='__main__':main()
