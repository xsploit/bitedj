# Engine cue import planning and persistence

`planEngineCueImport` merges detached cue values for one source library/track. Source hot cues and saved loops retain separate bank/slot identities and do not depend on their local control indexes. The caller supplies explicit disjoint control pools, excluding controls reserved by custom mappings. The planner never replaces occupied controls; unavailable capacity produces a conflict without accepting a baseline. Source deletions release controls before additions are allocated.

Cue position pairs are merged as one bounds field; labels and RGB colors can merge independently. Same-field conflicts keep local values and old baselines across repeats. Source deletion removes an unchanged imported cue, but preserves locally edited cues with a conflict. Unchanged source data respects a locally deleted cue. A later source edit conflicts with that local deletion. Accepted source deletions retain null tombstones, allowing a subsequent source recreation. A locally changed cue type is merged as a whole cue to avoid combining incompatible bounds. Legacy cues and cues from other source identities stay intact.

Input must be a validated reader track, with both complete source banks. The caller must first establish the same sample rate and frame origin in the source and decoded local audio. The explicit decoded frame limit bounds source positions; this code does not guess MP3 encoder delay, rescale positions, or clip invalid loops. A hot cue must be before the final frame boundary; a loop may end exactly on it. Invalid source bounds preserve the prior cue and baseline and report the field as unrepresentable. RGB is retained; non-opaque source alpha is reported as unrepresentable because Cue stores no alpha channel.

The planner handles plain value snapshots, not live QObjects. It does not publish changes, mutate Tracks, commit a transaction or expose an import action. The caller still needs a consistent snapshot and concurrent-edit protection, then staged writes and postcommit cache/model publication. Source identity baselines are scoped by library UUID and track ID. Returned database IDs stay stable for updates; zero identifies a new cue.

Run the production-archive merge probe:

```
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_cue_import_probe.cpp
```

For a reproducible reader-to-database test, build the standalone reader as described in `tools/engine-reader/README.md`. Build its opt-in fixture target and create a new temporary output directory:

```
cmake --build READER_BUILD --target engine-reimport-fixture
python tools/engine-reader/tests/prepare_reimport_fixture.py READER_BUILD/engine-reimport-fixture INSTALLED_LAUNCHER NEW_FIXTURE_DIRECTORY
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_cue_import_persistence_probe.cpp --probe-arg /absolute/repo/res/schema.xml --probe-arg NEW_FIXTURE_DIRECTORY/initial.json --probe-arg NEW_FIXTURE_DIRECTORY/updated.json
```

The generator produces two distinct playable 3-second stereo PCM WAVs at44.1kHz and Engine schema3.0.2 libraries with stable source identities. The persistence probe validates both packages, creates the actual BiteDJ schema0→42 and stages the planner output through production CueDAO and EngineImportRegistry. Initial import, rolled-back update, committed update, repeated import and reload preserve IDs, separate hot1/loop1, fractional frames and loop8 ending at132300. Its Cue materialization/snapshot adapter is test code with detached DAO objects; it does not establish safe publication to loaded player objects. Native Engine duplicate playlist membership is separately represented as a protocol-only fixture, because the generated native schema forbids it.

Cue materialization must map the detached `endFrame == Cue::kNoPosition` sentinel to `audio::kInvalidFramePos`, not `FramePos(-1)`: negative finite FramePos values are valid, while unset uses NaN. The persistence probe explicitly checks that hot-cue ends remain unset after database reload.


## Remaining integration work

The metadata importer currently preserves source timing in `deferredTiming`; it does not call this planner. The native decoder and track-load cue findings in `ENGINE_FRAME_ORIGIN.md` do not supply a live publication mechanism. `Track::replaceRecordIfUnchanged` compares TrackRecord only; it does not protect independently mutable Cue objects or the beat grid. `Track::setCuePoints` replaces the list and marks it dirty without checking an expected cue snapshot. Calling it after a staged database commit would leave an unprotected interval in which a user's cue edit could be overwritten.

Before exposing cue import, capture consistent cue/beat state, reject or retry concurrent edits, and establish how successful staged writes are published to the Track and loaded deck controls. Verify rollback, postcommit publication, repeated import, local edits during import, stable cue IDs and preserved FLX6 control assignments in the actual application. Frame-origin matching and native main-cue/beat-grid semantics remain separate requirements. The persistence probe exercises the current schema42, but its detached DAO objects do not prove these live-track behaviors.


## Existing-track cue save commit boundary

`TrackDAO::updateTrack` now uses `CueDAO::prepareTrackCueSave` inside its transaction, checks every cue write/delete, and calls `finishTrackCueSave` only after successful commit. Failed staging or commit leaves the original Cue IDs and dirty flags untouched. Successful commit attaches saved IDs to the original QObjects; it never replaces their identity. A cue edited during the SQL phase keeps its newer values and stays dirty, while a newly committed row ID is retained so retry updates that row. If a captured cue or the cue list changed, updateTrack returns false so the caller does not mark that Track clean or accept import provenance. This is not a general atomic snapshot of metadata, beats and every cue, and does not eliminate the separate live-import handoff requirements above.

New-track insertion has separate batch coverage described below. File analysis cache side effects remain outside the SQL rollback guarantee.

Run the commit-boundary regression against a completed application build:

```sh
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA \
  --probe-source /absolute/repo/os/tests/engine_cue_save_commit_probe.cpp \
  --probe-arg /absolute/repo/res/schema.xml
```

It uses synthetic schema42 data and the native SQLite commit hook to reject an outer commit. It covers partial cue write failure, delete failure, rollback preserving original IDs/dirty state, edits between preparation and commit, retry without duplicate IDs, and staging cues whose bounds were cleared. The full rebuilt app also passed the existing ordinary metadata/BPM save/restart check with eight unchanged cue rows, plus an injected cue-update failure leaving metadata, beat data and cue rows unchanged with SQLite integrity intact.


## New-track batches and scanner publication

New-track insertion stages cues without accepting their IDs or clean state until the outer transaction commits. A failed insert/cue write rejects the batch. Failed commit or explicit rollback invalidates provisional Track IDs, removes their registered cache IDs, restores the previous added date when unchanged, and leaves Track/Cue state dirty for retry. Successful commit accepts unchanged snapshots; intervening edits remain dirty. Callers receive failure instead of a nonexistent persisted ID.

The scanner announces newly inserted tracks only after successful commit. Canceling a scan retains the existing policy of committing accepted work; database failures roll back the insertion batch and skip successful-scan cleanup. Temporary Track IDs remain visible through the existing batch/cache API before commit; this is not a redesign of all provisional cache visibility or concurrent edits.

Run the DAO/cache and real scanner finish-handler tests against a completed build:

```sh
python3 os/tests/test_engine_cue_origin.py /absolute/build --ninja ninja \
  --probe-source os/tests/track_batch_commit_probe.cpp \
  --probe-arg /absolute/repo/res/schema.xml
QT_QPA_PLATFORM=offscreen python3 os/tests/test_engine_cue_origin.py /absolute/build --ninja ninja \
  --probe-source os/tests/scanner_commit_publication_probe.cpp \
  --probe-arg /absolute/repo/res/schema.xml
python3 os/tests/test_scanner_app.py --build /absolute/build --output /tmp/bitedj-scan-results
```

The first probe covers rejected commit, registered cache rollback/retry, second-track cue failure, explicit rollback and commit-only publication output. The second invokes the actual scanner finish slot with synthetic staged data; it does not start discovery workers. The application test uses the Rescan Library menu, nested synthetic WAV files and temporary profiles to check recursive discovery, repeated scanning without duplicates, injected insertion failure, unchanged media hashes and SQLite integrity after orderly exit. It does not use a real music collection or hardware.

Pending batches hold strong Track references through commit, so peak memory scales with new tracks in a scan. A private x86_64 synthetic experiment with 5,000 tracks and one cue each increased process RSS by about 19.5 MiB before commit. It contained no decoded audio or waveforms, included database/allocator overhead and had no old-code control; it is neither a worst-case bound nor a Pi performance result. Large real-library memory use remains a deployment consideration. These changes do not enable Engine cue/grid import or establish native Engine performance on the Pi.
