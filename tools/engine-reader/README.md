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

## Additive track metadata (protocolVersion 1)

The reader emits `comment`, `composer`, `publisher` as nullable strings;
`bitrateKbps` in kilobits/second; `ratingPercent` in 0–100; `year` and
`trackNumber` as nullable integers; and `fileBytes` as a nullable exact unsigned
64-bit decimal string. Empty strings are preserved separately from null.
Engine uses rating 0 as its unrated sentinel; libdjinterop returns null for it.
These are the dependency's interpreted values, not a lossless dump of SQL columns.
Older v1 producers may omit these additive fields; omission means unmanaged,
not a request to clear local metadata.

`keyId` is a nullable libdjinterop musical_key ordinal, **not a Mixxx key enum**.
The pinned mapping for IDs 0 through 23 is:
C major, A minor, G major, E minor, D major, B minor, A major, F-sharp minor,
E major, D-flat minor, B major, A-flat minor, F-sharp major, E-flat minor,
D-flat major, B-flat minor, A-flat major, F minor, E-flat major, C minor,
B-flat major, G minor, F major, D minor.
Consumers must translate explicitly rather than cast to their own key enum.

The synthetic generator `tests/create_metadata_fixture.cpp` creates 26 tracks
covering every key, absent and empty metadata, zero values, and file size
9007199254740993 (above JavaScript's exact integer range). Compile it against
the same pinned dependency and run `python3 tests/check_metadata.py
/path/to/generator /path/to/stage/libexec/bitedj-engine/engine-import.py`.
This proves the database-to-launcher path without firmware or audio fixtures.
Waveform data, artwork, loudness, play history, remixer and streaming-specific
metadata are not exposed by this protocol yet.

`tests/check_raw_metadata.py` takes the same two executable paths and injects
metadata directly into synthetic SQL databases for schemas 3.0.0 and 3.0.2.
It checks all24 key IDs and all five nonzero rating levels independently of the
fixture writer's conversions, and verifies source database bytes stay unchanged.
The generator accepts an optional `3.0.0` argument for that older schema.
Legacy v1 `title` normalizes missing SQL title to an empty display string.
The additive nullable `sourceTitle` preserves the actual SQL title: null for
missing, an empty string for explicitly empty, and text otherwise. Use
`sourceTitle` for import policy; keep `title` for display compatibility. Older
v1 producers omit `sourceTitle`, which must remain unmanaged rather than
be interpreted as a request to clear local data. Direct SQL tests distinguish
NULL from empty title on both supported schemas.

Normal Linux cancellation with SIGTERM now unwinds the launcher, which kills
and reaps the directly running reader and removes its temporary snapshot.
SIGINT also unwinds through Python's interrupt handling. The regression test
`python3 tests/check_cancellation.py` starts the actual launcher with a minimal
synthetic library and an instrumented adjacent reader, waits for that reader to
start, cancels, and verifies child termination, snapshot cleanup and no output
from the interrupted reading phase. It passes on desktop and Pi. Before the fix,
SIGTERM demonstrably left the reader alive. SIGKILL cannot run cleanup; an app
should request terminate first and escalate only after a bounded grace period.
Only accept output after a successful launcher exit and complete validation:
cancellation during final output emission could leave an incomplete document.

## Preserved main-cue state

Tracks additionally include optional `mainCueState` with finite numeric `defaultFrame` and `adjustedFrame`, and boolean `isAdjusted`. These are the raw low-level cue fields, including zero values. The existing `mainCueFrame` remains the high-level libdjinterop snapshot value for compatibility; it can be null even when a nonzero default cue is stored. Python and C++ validators accept older packages without `mainCueState` and reject malformed state when present.

BiteDJ retains this object in the track's deferred timing provenance. It does not select a playable cue from these fields or apply a decoder-origin correction. Native selection semantics and database-to-audio alignment remain separate verification gates.
