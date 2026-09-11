"""Compile the actual parent-side resolver against synthetic Linux paths."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='bitedj-media-resolver-') as temporary:
    exe = Path(temporary) / 'probe'
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'Qt6Core'], text=True))
    subprocess.run(['c++', '-std=c++20', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'src'), str(Path(__file__).with_name('engine_media_resolver_probe.cpp')),
                    str(root / 'src/library/engine/enginemediaresolver.cpp'), *flags, '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
