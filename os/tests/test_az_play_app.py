#!/usr/bin/env python3
"""Exercise the real AZ play layout in Xvfb with disposable synthesized media.
Requires Linux, Qt6 development packages, ALSA null PCM, and an existing app build.
No user profile, music, controller, physical output or Pi is used.
"""
import argparse
import array
import hashlib
import math
import os
from pathlib import Path
import select
import shlex
import shutil
import subprocess
import tempfile
import wave

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--xvfb', default=shutil.which('Xvfb'))
a = p.parse_args()
if not a.xvfb: p.error('Install Xvfb or pass --xvfb /path/to/Xvfb')
repo = Path(__file__).resolve().parents[2]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='bitedj-az-play-') as temporary:
    root = Path(temporary)
    profile = root/'profile'
    profile.mkdir()
    (profile/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n[BiteDJ]\nmain_view_style 3\nvisual_theme 0\n[Waveform]\nWaveformType 19\n')
    (root/'alsa.conf').write_text('pcm.!default { type null }\nctl.!default { type null }\n')
    (profile/'soundconfig.xml').write_text('''<!DOCTYPE SoundManagerConfig>
<SoundManagerConfig api="ALSA" deck_count="4" latency="5" samplerate="44100" sync_buffers="2"><SoundDevice name="default" portAudioIndex="0"><output channel="0" channel_count="2" index="0" type="Master"/></SoundDevice></SoundManagerConfig>''')
    media=[]
    for n in (1,2):
        samples=array.array('h')
        for i in range(44100*60):
            t=i/44100
            envelope=0.03+0.5*math.exp(-((t*128/60)%1)*14)
            v=int(8000*envelope*(math.sin(2*math.pi*110*n*t)+0.4*math.sin(2*math.pi*(330+n*15)*t)))
            samples.extend((v,v))
        path=root/f'UI-demo-{n}.wav'
        with wave.open(str(path),'wb') as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(44100); f.writeframes(samples.tobytes())
        media.append(str(path))
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets','Qt6Test'],text=True))
    subprocess.run(['c++','-std=c++20','-fPIC','-shared',str(repo/'os/tests/az_play_app_driver.cpp'),'-o',str(root/'driver.so'),*flags],check=True)
    with (out/'xvfb.log').open('w') as log:
        server=subprocess.Popen([a.xvfb,'-displayfd','1','-screen','0','1280x800x24','-nolisten','tcp'],stdout=subprocess.PIPE,stderr=log,text=True)
        try:
            if not select.select([server.stdout],[],[],15)[0]: raise TimeoutError('Xvfb startup')
            display=server.stdout.readline().strip()
            assert display.isdigit(), 'Xvfb did not provide a display'
            env=dict(os.environ,DISPLAY=':'+display,QT_QPA_PLATFORM='xcb',QT_QPA_PLATFORMTHEME='',LIBGL_ALWAYS_SOFTWARE='1',ALSA_CONFIG_PATH=str(root/'alsa.conf'),LD_PRELOAD=str(root/'driver.so'),AZ_CAPTURE=str(out/'play'))
            binary=a.build.resolve()/'mixxx'
            for phase in ('controls','persistence'):
                env['AZ_EXPECT_COLLAPSED']='1' if phase=='persistence' else '0'
                with (out/f'{phase}.log').open('w') as log:
                    subprocess.run([str(binary),'--settings-path',str(profile),'--resource-path',str(repo/'res'),*media],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,check=True)
                assert 'AZ_PLAY '+('PERSISTENCE PASS' if phase=='persistence' else 'PASS') in (out/f'{phase}.log').read_text()
            (out/'binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
            print('PASS: panel independence/resize/reuse, time, quantize/keylock, loop start/exit, beat jump, hot cues, drawer, zoom/grid access, layout switching, restart persistence')
        finally:
            server.terminate()
            server.wait(timeout=5)
