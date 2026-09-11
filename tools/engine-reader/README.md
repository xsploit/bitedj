# Isolated Engine library reader for BiteDJ

This Linux prototype reads Engine schema3.0.0 and3.0.2 into version1 JSON. It does not implement BiteDJ's import transaction or UI. The existing exporter keeps its own dependency.

Build with CMake3.24+, a C++20 compiler, Git, Qt6 Core development files, SQLite3 and zlib development files:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/stage"
cmake --build build -j4
cmake --install build
stage/libexec/bitedj-engine/engine-import.py '/media/usb/Engine Library' --media-root /media/usb
```

Python3 and its SQLite module are needed at runtime. On Linux the helper finds its adjacent shared library via `$ORIGIN`. The launcher selects its adjacent `engine-reader` executable; callers do not supply arbitrary helper paths. Tested relocation to a temporary directory with no LD_LIBRARY_PATH and an unrelated working directory.

The dependency is pinned to xsco/libdjinterop commit99b5130a70232cfae1666dd268f7b9a05bbaaaa6. Archive SHA256: 61c89760569b8ac87df0b8f19d8e70f195e94d47622f196c27c51e993423cd13. To use an offline copy, set `-DBITEDJ_ENGINE_ARCHIVE=/absolute/path/to/archive.tar.gz`. The same hash is verified for local and downloaded archives. The patches retain extended track blobs, accept the observed empty overview marker, and bound compressed-data parsing. Snapshot-writing patches are deliberately absent from this reader package. Dependency license is installed with the package; source archive URL and patches are retained here for rebuilding.

The launcher snapshots Database2/m.db using SQLite backup, including committed WAL data, before running readers. Original track/playlist IDs and separate cue/loop banks are retained. Frames remain fractional where present; sampleCount is an exact decimal string. Relative media paths resolve against the original Engine Library directory within the explicitly selected media root. Missing/outside-root media is reported, not silently omitted. Path resolution is an inspection result; the importer must revalidate before opening audio.

Current limits: individual compressed/decompressed blobs128MiB; child address space2GiB, CPU/wall60seconds, and stdout/stderr64MiB each. The Linux launcher also limits its own address space to 1 GiB (hard ceiling 2 GiB), CPU to 90 seconds and wall time to 120 seconds; snapshots are capped at 2 GiB, playlists at 100,000, memberships at 1,000,000 and final JSON at 64 MiB. Hashing and final JSON emission are streamed. These are resource limits, not a filesystem sandbox. Linux process limits, Qt dependencies, malformed nested structures and large-library scaling still require integration review. Windows packaging is not validated. The application must validate protocol data again, apply local-edit/idempotence/conflict rules transactionally and expose unresolved references. A successful reader run is not proof of a successful BiteDJ import.


## Portable regression tests

From the repository root run `python3 -m unittest discover -s tools/engine-reader -p 'test_*.py'`. The 13 tests create synthetic records and temporary databases; no private library, music or firmware is required. They cover separate banks, invalid protocol values, malformed child output, process time/output limits, database/output quotas, playlist graphs and repeated entries, media-root resolution and committed WAL snapshot preservation. These tests do not replace compiled-parser sanitizer tests or full application import acceptance.

This is a standalone CMake project. Do not add it as a subdirectory of the application build: its dependency options belong to its isolated configuration. The main application continues to use its existing exporter dependency. This package does not yet register an Engine import action or modify the local library.
