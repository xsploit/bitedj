#!/usr/bin/env python3
"""Test device power cancellation and refusal in the real app, without powering off.
Requires Linux/logind (permission for a temporary shutdown delay inhibitor),
Qt6 development tools and Xvfb. Uses a disposable profile and null audio.
"""
import argparse
import os
from pathlib import Path
import select
import shlex
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--xvfb', default=shutil.which('Xvfb'))
args = parser.parse_args()
if not args.xvfb:
    parser.error('Install Xvfb or pass --xvfb /path/to/Xvfb')
repo = Path(__file__).resolve().parents[2]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='bitedj-power-') as temporary:
    root = Path(temporary)
    profile = root/'profile'
    profile.mkdir()
    (root/'bin').mkdir()
    shim = root/'bin/systemctl'
    shim.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$POWER_TEST_LOG"\necho "Test refusal: device power denied" >&2\nexit 1\n')
    shim.chmod(0o700)
    (profile/'mixxx.cfg').write_text('[Config]\nVersion 2.5.6\n[BiteDJ]\nmain_view_style 3\n[Waveform]\nWaveformType 19\n')
    (root/'alsa.conf').write_text('pcm.!default { type null }\nctl.!default { type null }\n')
    (profile/'soundconfig.xml').write_text('''<!DOCTYPE SoundManagerConfig>
<SoundManagerConfig api="ALSA" deck_count="4" latency="5" samplerate="44100" sync_buffers="2"><SoundDevice name="default" portAudioIndex="0"><output channel="0" channel_count="2" index="0" type="Master"/></SoundDevice></SoundManagerConfig>''')
    flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Widgets','Qt6Test'], text=True))
    subprocess.run(['c++','-std=c++20','-fPIC','-shared',str(repo/'os/tests/device_power_app_driver.cpp'),'-o',str(root/'driver.so'),*flags], check=True)
    with (out/'xvfb.log').open('w') as log:
        server = subprocess.Popen([args.xvfb,'-displayfd','1','-screen','0','1280x800x24','-nolisten','tcp'], stdout=subprocess.PIPE, stderr=log, text=True)
        try:
            if not select.select([server.stdout],[],[],15)[0]:
                raise TimeoutError('Xvfb startup')
            display = server.stdout.readline().strip()
            assert display.isdigit()
            env = dict(os.environ, DISPLAY=':'+display, QT_QPA_PLATFORM='xcb', QT_QPA_PLATFORMTHEME='', LIBGL_ALWAYS_SOFTWARE='1', ALSA_CONFIG_PATH=str(root/'alsa.conf'), LD_PRELOAD=str(root/'driver.so'), POWER_TEST_DIR=str(root), POWER_TEST_LOG=str(root/'requests.log'), PATH=str(root/'bin')+':'+os.environ['PATH'])
            with (out/'ui.log').open('w') as log:
                subprocess.run([str(args.build.resolve()/'mixxx'),'--settings-path',str(profile),'--resource-path',str(repo/'res')], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=45, check=True)
            for artifact in list(root.glob('*.png')) + list(root.glob('*.log')):
                shutil.copy2(artifact, out/artifact.name)
            assert 'POWER REQUEST TEST PASS' in (out/'ui.log').read_text()
            print('PASS: cancellation, distinct reboot/poweroff, refusal recovery and retry')
        finally:
            server.terminate()
            server.wait(timeout=5)
