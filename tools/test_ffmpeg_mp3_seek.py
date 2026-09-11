#!/usr/bin/env python3
"""Regression test against a completed Linux CMake/Ninja application build.

Usage: python tools/test_ffmpeg_mp3_seek.py BUILD_DIR --ninja /path/to/ninja
Requires ffmpeg with libmp3lame and compile_commands.json. Uses synthetic audio;
compares random reads with sequential output from the actual BiteDJ decoder.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_dir', type=Path)
parser.add_argument('--ninja', default='ninja')
args = parser.parse_args()
build = args.build_dir.resolve()
source = Path(__file__).with_name('ffmpeg_mp3_seek_probe.cpp').resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
main = next(c for c in commands if c['file'].endswith('/src/main.cpp'))
with tempfile.TemporaryDirectory(prefix='bitedj-mp3-seek-') as temporary:
    root = Path(temporary)
    obj, binary = root / 'probe.o', root / 'probe'
    compile_args = shlex.split(main['command'])
    compile_args[compile_args.index('-o') + 1] = str(obj)
    compile_args[compile_args.index('-c') + 1] = str(source)
    subprocess.run(compile_args, cwd=main['directory'], check=True)
    lines = subprocess.check_output(
        [args.ninja, '-C', str(build), '-t', 'commands', 'mixxx'], text=True).splitlines()
    link = shlex.split(lines[-1])
    if link[:2] == [':', '&&']:
        link = link[2:]
    if '&&' in link:
        link = link[:link.index('&&')]
    link[link.index('CMakeFiles/mixxx.dir/src/main.cpp.o')] = str(obj)
    link[link.index('-o') + 1] = str(binary)
    subprocess.run(link, cwd=build, check=True)
    wav = root / 'source.wav'
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
        'aevalsrc=0.3*sin(2*PI*(440*t+60*t*t))|0.2*sin(2*PI*(719*t+30*t*t)):s=44100:d=5',
        '-c:a', 'pcm_s16le', str(wav)], check=True)
    fixtures = [wav]
    for channels in (1, 2):
        for mode, flags in [('cbr', ['-b:a', '192k']), ('vbr', ['-q:a', '2'])]:
            path = root / f'{mode}-{channels}ch.mp3'
            subprocess.run(['ffmpeg', '-v', 'error', '-i', str(wav), '-ac', str(channels),
                '-c:a', 'libmp3lame', *flags, str(path)], check=True)
            fixtures.append(path)
    for path in fixtures:
        subprocess.run([str(binary), str(path)], check=True)
        print(f'PASS {path.name}', flush=True)
