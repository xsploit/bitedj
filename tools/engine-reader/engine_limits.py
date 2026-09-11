"""Linux launcher limits and bounded JSON emission for the Engine reader."""
import json
import resource
import shutil
import signal
import tempfile


def apply_launcher_limits():
    # QProcess::terminate sends SIGTERM on Linux. Unwind Python scopes so
    # subprocess.run kills/reaps the direct reader and TemporaryDirectory
    # removes its snapshot. Default SIGTERM exits without this cleanup.
    def cancelled(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, cancelled)
    def limit(which, soft, hard):
        _, inherited = resource.getrlimit(which)
        ceiling = hard if inherited == resource.RLIM_INFINITY else min(hard, inherited)
        resource.setrlimit(which, (min(soft, ceiling), ceiling))
    limit(resource.RLIMIT_AS, 1024**3, 2*1024**3)
    limit(resource.RLIMIT_CPU, 90, 90)
    limit(resource.RLIMIT_FSIZE, 2*1024**3, 2*1024**3)
    limit(resource.RLIMIT_CORE, 0, 0)
    def expired(signum, frame):
        raise TimeoutError('Engine import exceeded 120 seconds')
    signal.signal(signal.SIGALRM, expired)
    signal.alarm(120)


def write_json_bounded(data, output, max_bytes=64*1024*1024):
    # Stage a complete validated document before emitting any success output.
    encoder = json.JSONEncoder(indent=2, allow_nan=False)
    with tempfile.TemporaryFile(mode='w+', encoding='utf-8') as temp:
        size = 0
        for chunk in encoder.iterencode(data):
            size += len(chunk.encode('utf-8'))
            if size + 1 > max_bytes:
                raise ValueError('Engine import JSON exceeds output limit')
            temp.write(chunk)
        temp.write('\n')
        temp.seek(0)
        shutil.copyfileobj(temp, output, 1024*1024)
