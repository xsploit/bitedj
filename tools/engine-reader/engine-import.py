#!/usr/bin/env python3
"""Read a selected Engine library using the adjacent pinned helper executable."""
import argparse
import sys
from pathlib import Path
from engine_import_package import package
from engine_limits import apply_launcher_limits, write_json_bounded
apply_launcher_limits()
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path)
p.add_argument('--media-root',type=Path)
a=p.parse_args()
write_json_bounded(package(a.source,Path(__file__).resolve().with_name('engine-reader'),a.media_root),sys.stdout)
