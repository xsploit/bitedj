#!/usr/bin/env python3
"""Test actual ANLZ import against a completed Linux CMake/Ninja application.

Usage: python3 os/tests/test_rekordbox_runtime.py BUILD_DIR --ninja /path/to/ninja
Uses synthetic files and temporary Tracks, never an installed profile.
The completed build must correspond to this source checkout.
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
entries = json.loads((build / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('/src/main.cpp'))
source = Path(__file__).with_name('rekordbox_runtime_probe.cpp').resolve()
with tempfile.TemporaryDirectory(prefix='bitedj-rekordbox-runtime-') as temporary:
    obj, exe = Path(temporary) / 'probe.o', Path(temporary) / 'probe'
    original = list(entry.get('arguments') or shlex.split(entry['command']))
    compile_args = []
    i = 0
    while i < len(original):
        if original[i] in ('-c', '-o', '-MF', '-MT', '-MQ'):
            i += 2
        else:
            compile_args.append(original[i])
            i += 1
    subprocess.run(compile_args + ['-c', str(source), '-o', str(obj)],
                   cwd=entry['directory'], check=True)
    commands = subprocess.check_output(
        [args.ninja, '-C', str(build), '-t', 'commands', 'mixxx'], text=True)
    links = [shlex.split(line) for line in commands.splitlines()]
    links = [line for line in links if '-o' in line and line[line.index('-o') + 1] == 'mixxx']
    if len(links) != 1:
        raise RuntimeError('Expected exactly one application link command')
    link = links[0]
    link = link[next(i for i, v in enumerate(link) if Path(v).name in ('c++', 'g++', 'clang++')):]
    if '&&' in link:
        link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(exe)
    link = [arg for arg in link if not arg.endswith('.o')]
    link.insert(1, str(obj))
    subprocess.run(link, cwd=build, check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
