# PR #1 review — Engine library import

Review date: 2026-09-13. Base: `e68d9b9536a5fb48a4553b858aa6b83b24e4805f`.
Incoming PR head: `02deb46aae9194ba0136be9f54efb016ee767e5d`.

The incoming PR contains 117 changed files. This review examined its application,
reader, shared database, test and research-document changes. The review fixes add
twelve previously unchanged files. The file ledger below covers those 129 files;
this review document is the additional file.

## Acceptance follow-up

The initial review was insufficient as full acceptance: it used a selected unit
suite and small app fixtures. The follow-up ran the complete enabled test suite,
provided disposable mount fixtures for the storage tests, exercised both supported
Engine schemas, and added larger-library and concurrent decoded-audio checks.

- **1,005 enabled unit tests passed across two fixture configurations.** The full
  run passed 1,004 and skipped the single-drive test because additional drives
  were visible. That test then passed in its own private namespace with `/mnt`
  and `/run/media` overlaid. An earlier rerun without the `/run/media` overlay
  still skipped and is not counted. Thirteen pre-existing `DISABLED_` tests
  remain disabled (soft takeover timing, three keylock toggles, core startup).
- The tested unit binary SHA-256 is
  `386432bb08bb7eaa601067b3119ae7d8416b7b6ae9621ed7de40d9636be41a7e`.
  The application binary is unchanged from the initial review hash below.
- Five pre-existing fixture assumptions were corrected without changing player
  behavior: both player-manager/sampler fixtures now create the preview deck
  before Library/EDMC setup; Opus cover testing requires an actual Opus provider;
  touch selection uses Qt's laid-out row rather than a font-dependent height;
  the offscreen USB-row test taps its post-scroll coordinates; MP3 first-sound
  expectations distinguish FFmpeg from MAD. An independent FFmpeg CLI decode
  with unity-gain mono-to-stereo duplication confirmed the MP3 sample references.
- The two real decoded deck buffers remained finite and non-silent while the
  real coordinator imported 500 tracks on the GUI thread. The full-suite run
  observed 40 audio callbacks during import, 40 audible buffers on each deck,
  maximum processing time 832 microseconds, and a 472 ms import. The paced
  callback interval was 11.61 ms. These are observed desktop values, not latency
  guarantees or a physical sound-device/analysis stress test.

| Real preview + Apply fixture | Preview | Apply | Largest Apply GUI timer gap | Sampled app peak RSS |
| --- | ---: | ---: | ---: | ---: |
| 1,000 tracks, Engine 3.0.2 | 334 ms | 1,140 ms | 13 ms | 554.5 MiB |
| 5,000 tracks, Engine 3.0.2 | 1,324 ms | 14,555 ms | 63 ms | 599.8 MiB |
| 1,000 tracks, Engine 3.0.0 | 310 ms | 1,141 ms | 15 ms | 555.5 MiB |

All three cases preserve the exact ordered playlist, source database and audio
bytes, track/provenance counts and SQLite integrity. The 10 ms timer measures
GUI event-loop gaps, not display FPS. RSS excludes reader children. Hardlinked
synthetic WAVs do not model slow physical USB or arbitrary music libraries.
Commands and test limits are in [ENGINE_APPLY.md](../os/tests/ENGINE_APPLY.md).

**Still open:** install/run the reviewed build on the Pi and test physical touch,
FLX6 playback/headphones, browse/Back/View, and import plus analysis while audio
is output. Hardware availability was requested; no Pi session occurred in this
review. The user subsequently authorized merging the desktop-verified work
without the Pi. The desktop evidence supports source integration; physical
acceptance remains a release/deployment gate, not a claim made by this merge.

The controller audit found no changes to `res/controllers` or `res/skins` in
PR #1 and no FLX4-specific assumption in the added Engine application code.
Both FLX4 and FLX6 mappings already exist in the base. Future Pablo-derived work
must be checked against the user's preferred FLX6 behavior independently.

## Finding and scope

The metadata/playlist workflow is implemented and exercised through the actual
Linux application. The desktop checks support merging the fixes and import workflow below. This is not approval of Pi performance or a live-show deployment.

The user-facing change is **Library → Preview Engine DJ library… → Import metadata
+ playlists**. Local edits, playlist occurrence IDs and source provenance survive
repeat imports. Missing/unresolved members defer a whole playlist rather than
silently shortening it. Cancellation preserves completed items. Metadata saves
and provenance are separate operations; the entire import is not atomic.

Cues, loops, BPM/beat grids, stems and streaming are **not applied/enabled** by this
PR. Cue planning/staging code is preparation. The native firmware diagnostics and
RX3 notes are optional historical research, not a new runtime dependency or proof
of complete compatibility. Existing FLX6 mappings and player skins are unchanged.

## Problems corrected during review

1. **Batch writes after rollback.** A SQLite statement could abort the transaction,
   but later tracks in the same batch could still write in autocommit mode. A
   regression failed the incoming archive with exit 44 and unexpected persisted
   rows. The DAO now rejects writes after a batch failure or lost transaction;
   rollback, row counts, cache identity and a fresh successful retry pass.
2. **Drive disappears after preview.** Resolver construction threw through the Qt
   Apply slot and aborted the incoming app (SIGABRT). Apply now reports a
   recoverable reconnect message. Renaming a disposable fixture away and back
   reproduces disappearance and verifies successful retry. This does not simulate
   electrical USB failure during playback.
3. **Never-added track save.** Included the fix previously proposed in PR #6:
   preserve pending dirty/export state on an unsaved cached track instead of
   treating it as a purged library track. Two real save/purge tests pass.
4. **Optional overview control.** Broader cue-control tests hit an assertion in
   pre-existing main-branch code when the skin's overview control was absent.
   The lookup is now explicitly optional. The existing 16 cue-control tests pass
   without suppressing assertions or adding a fake skin control.
5. **Diagnostic finite check.** `std::isfinite` was optimized to true by this
   build's fast-math flags. The PCM diagnostic now calls the project's separately
   compiled finite-check helper. A controlled finite/NaN/+Inf/-Inf compiler
   experiment rejected all three nonfinite values; the old expression accepted
   them. This changes a diagnostic, not the audio engine.
6. Older component notes now point to the implemented Apply workflow, while
   retaining their historical checkpoints and remaining cue-integration limits.

## Validation

All final app checks used executable SHA-256
`d78ad954e25aaaa1c4656d8005fd04391545a3706174f8bda3b230192164b53e`.
The app and test executable rebuilt successfully after the final production edit.
The standalone reader was built/installed from this PR's source and its pinned
dependency archive, not reused from a different branch.

| Check | Result |
| --- | --- |
| Relevant existing/new unit tests | 58 passed across 17 suites, including metadata save, track save/purge, cues, cache, scanner, playlists and schema |
| Production-archive import/database probes | 12 passed: batch, scanner publication, pending cues, staged records/cues, playlist identity/staging/merge, metadata, live record publication, cue planning/persistence |
| Parent protocol validator | 73 cases passed with optimized fast-math compile flags |
| Other directly compiled components | Merge/registry, media resolver and asynchronous reader-service suites passed |
| Standalone Python suite | 14 unittest cases passed; standalone module checks also completed |
| Fresh installed reader | Metadata, raw SQLite fields, cancellation and persistence-fixture preparation passed |
| Actual app: normal Apply | 9 scenarios passed: initial, repeat, source update/local conflict, cancellation, missing member, failed save, name collision, repeat collision, source rename |
| Actual app: focused Apply | 2 main-cue provenance cases, 2 changed/outside-root media cases and 1 disconnect/reconnect case passed |
| Actual app: portable ratings | The 9 Apply scenarios passed in a private tmpfs mount namespace, preserving four-star and explicit zero-star overrides |
| Actual app: preview | Both development override and installed-helper discovery passed; source DB unchanged and zero tracks imported |
| Actual app: scanner | Recursive/repeated scan and rejected insertion passed, media unchanged and integrity valid |
| Actual audio providers | Synthetic WAV, CBR/VBR/no-Xing MP3, AAC and FLAC range/PCM/seek diagnostic passed; complete gapless MP3 length check enabled |
| Optional research files | JSON parsed, Python syntax checked, code/data scope reviewed; native firmware experiments not rerun |

That is 27 final full-app scenario executions, separate from unit/component tests.
Apply checks inspect persistent rows/identities, source and media hashes, absence
of applied Engine cues and SQLite integrity. The preview screenshot was inspected
for readable table/status/control layout; this is an offscreen desktop rendering.
The 12 archive probes and audio diagnostic ran before the last optional-overview
lookup edit; their import/database/audio source was unchanged by that edit. The
58-unit and all 27 full-app checks ran after the final production rebuild.

The first broad cue run stopped on the missing-control assertion described above;
it is not counted as passing. One scanner test launch collided with the final
link step and failed to execute; the final post-build scanner run passed. A first
manual batch invocation used a relative schema path from a temporary directory;
the corrected absolute-path probe passed. No such incomplete run is counted as
acceptance evidence.

### Reproduction entry points

Build `mixxx` and `mixxx-test` with testing enabled and follow the separate reader
build/install steps in [the reader README](../tools/engine-reader/README.md).
The full-app tests and focused flags are in [ENGINE_APPLY.md](../os/tests/ENGINE_APPLY.md).
Component/probe commands are linked from the corresponding documents in the ledger.

```sh
QT_QPA_PLATFORM=offscreen BUILD/mixxx-test \
  --gtest_filter='TrackDAOTest.*:*saveTrackMetadata*:CueTest.*:CueControlTest.*:GlobalTrackCacheTest.*:LibraryScannerTest.*:PlaylistTest.*:SchemaManagerTest.*:DbConnectionPoolTest.*:TrackTest.*:TrackCollectionTest.*:TrackMetadataTest.*:SqlTransactionTest.*' \
  --resource-path /absolute/repo/res --logLevel info
python3 -m unittest discover -s tools/engine-reader -p 'test_*.py'
python3 os/tests/test_engine_import_package.py
python3 os/tests/test_engine_import_state.py
python3 os/tests/test_engine_media_resolver.py
python3 os/tests/test_engine_reader_service.py
python3 os/tests/test_engine_cue_origin.py BUILD --ninja NINJA \
  --probe-source /absolute/repo/os/tests/track_batch_commit_probe.cpp \
  --probe-arg /absolute/repo/res/schema.xml
python3 tools/test_audio_timeline.py BUILD --ninja NINJA \
  --output /path/to/audio-results.json --require-complete-mp3
```

## Limits and follow-up

- Linux x86_64, synthetic temporary profiles/audio and an installed standalone
  reader were used. No user's music/profile or Pi was modified by these checks.
- Pi touch, FLX6 physical operation, Windows runtime and physical audio output
  under concurrent analysis/import still require device testing. The added
  concurrent-buffer fixture is a narrower PC check. No performance gain is claimed.
- The larger synthetic desktop import measurements above are not a worst-case
  bound. Reading is in a worker, but Apply does synchronous per-item work on the
  GUI thread, and a scan retains strong references until its batch commits.
- Engine helper installation is separate from the app build. Preview reports a
  missing helper; it does not silently install one. Apply requires native SQLite
  support. Synthetic Engine fixtures now exercise both accepted schemas, 3.0.0 and
  3.0.2; arbitrary real exports remain a separate acceptance case.
- No explicit take-source conflict UI exists. Local values are retained and
  conflicts reported. Source timing remains provenance until alignment and safe
  live publication are validated. External-collection synchronization is not an
  end-to-end acceptance result here.
- Optional firmware diagnostics were reviewed as research code and data, not
  rerun. Firmware binaries, keys and artwork are not added by this review.
- GitHub reported no automated status checks for this PR; local evidence is
  listed explicitly rather than implying a green CI workflow.

## File review ledger

Each row states the role checked and its evidence boundary. A passing synthetic
probe is not equivalent to physical hardware or full feature validation.

| File | Review result / boundary |
| --- | --- |
| [`CMakeLists.txt`](../CMakeLists.txt) | Registers the Engine application sources; standalone reader installation stays separate. |
| [`docs/rx3-browser-wave-response-findings.md`](../docs/rx3-browser-wave-response-findings.md) | Historical research/design notes only; no waveform or browser feature is activated by this file. |
| [`docs/rx3-pablo-reuse-checkpoint.md`](../docs/rx3-pablo-reuse-checkpoint.md) | Historical research ledger, including work outside this PR; counts and firmware observations were not independently re-executed during this review. |
| [`os/tests/ENGINE_APPLY.md`](../os/tests/ENGINE_APPLY.md) | Current user-visible scope, partial commits, conflicts, portable ratings, disconnect/retry and reproducible app checks. |
| [`os/tests/ENGINE_CUE_IMPORT.md`](../os/tests/ENGINE_CUE_IMPORT.md) | Detached cue planner/staging documentation; explains why live cue/grid import remains disabled. |
| [`os/tests/ENGINE_FRAME_ORIGIN.md`](../os/tests/ENGINE_FRAME_ORIGIN.md) | Timing research and unresolved cross-decoder alignment; not a cue compatibility guarantee. |
| [`os/tests/ENGINE_IMPORT_PACKAGE.md`](../os/tests/ENGINE_IMPORT_PACKAGE.md) | Typed protocol validation and fixture command. |
| [`os/tests/ENGINE_IMPORT_STATE.md`](../os/tests/ENGINE_IMPORT_STATE.md) | Registry/three-way merge contract; review labels older checkpoints as historical. |
| [`os/tests/ENGINE_METADATA_IMPORT.md`](../os/tests/ENGINE_METADATA_IMPORT.md) | Supported metadata planning and deferred timing; review adds current-integration pointer. |
| [`os/tests/ENGINE_PLAYLIST_MERGE.md`](../os/tests/ENGINE_PLAYLIST_MERGE.md) | Occurrence identity, local/source conflicts and retry behavior; historical scope clarified. |
| [`os/tests/ENGINE_PLAYLIST_STAGING.md`](../os/tests/ENGINE_PLAYLIST_STAGING.md) | Transaction, savepoint and publication contract; historical scope clarified. |
| [`os/tests/ENGINE_PREVIEW_UI.md`](../os/tests/ENGINE_PREVIEW_UI.md) | Reader install/discovery and read-only preview workflow; desktop scope clearly limited. |
| [`os/tests/ENGINE_READER_SERVICE.md`](../os/tests/ENGINE_READER_SERVICE.md) | Background process protocol, cancellation and bounded output; historical scope clarified. |
| [`os/tests/ENGINE_STAGED_WRITES.md`](../os/tests/ENGINE_STAGED_WRITES.md) | Detached database staging contract and limits; historical scope clarified. |
| [`os/tests/ENGINE_TRACK_PUBLICATION.md`](../os/tests/ENGINE_TRACK_PUBLICATION.md) | Conditional live-record publication and non-atomic SQL/live-cue limits. |
| [`os/tests/engine_apply_app_driver.cpp`](../os/tests/engine_apply_app_driver.cpp) | Test-only real Qt widget driver; injects save failures and simulated disappearance without replacing importer code. |
| [`os/tests/engine_cue_import_persistence_probe.cpp`](../os/tests/engine_cue_import_persistence_probe.cpp) | Installed reader fixture through real schema42, cue planner, CueDAO and registry; passed. |
| [`os/tests/engine_cue_import_probe.cpp`](../os/tests/engine_cue_import_probe.cpp) | Detached cue banks, capacity, bounds, identity, type transitions and field conflicts; passed, not enabled in Apply. |
| [`os/tests/engine_cue_save_commit_probe.cpp`](../os/tests/engine_cue_save_commit_probe.cpp) | Actual pending cue save, failures and intervening edits across commit; passed. |
| [`os/tests/engine_import_state_probe.cpp`](../os/tests/engine_import_state_probe.cpp) | Merge/registry identity scopes, rollback, corrupt state and persistent conflicts; passed. |
| [`os/tests/engine_media_resolver_probe.cpp`](../os/tests/engine_media_resolver_probe.cpp) | Canonical containment, missing/foreign paths and symlink retargeting; passed. |
| [`os/tests/engine_metadata_import_probe.cpp`](../os/tests/engine_metadata_import_probe.cpp) | Supported metadata fields, independent conflicts, deferred timing and portable source-rating behavior; passed. |
| [`os/tests/engine_playlist_identity_probe.cpp`](../os/tests/engine_playlist_identity_probe.cpp) | Schema42 migration and deletion/reused-row identity protection; passed. |
| [`os/tests/engine_playlist_merge_probe.cpp`](../os/tests/engine_playlist_merge_probe.cpp) | Real DAO/registry reimport, source/local occurrence edits and persistent conflicts; passed. |
| [`os/tests/engine_playlist_staging_probe.cpp`](../os/tests/engine_playlist_staging_probe.cpp) | Savepoint recovery, rollback, commit failure, IDs, cache/signals and lost transaction; passed. |
| [`os/tests/engine_preview_app_driver.cpp`](../os/tests/engine_preview_app_driver.cpp) | Test-only menu/dialog driver; invalid selection, missing helper, cancellation and reopen. |
| [`os/tests/engine_reader_json_cases.py`](../os/tests/engine_reader_json_cases.py) | Raw JSON corpus retains duplicate keys and malformed grammar; service suite passed. |
| [`os/tests/engine_reader_service_probe.cpp`](../os/tests/engine_reader_service_probe.cpp) | Production async service event-loop harness; responsiveness/cancellation and parent-owned path context. |
| [`os/tests/engine_staged_cues_probe.cpp`](../os/tests/engine_staged_cues_probe.cpp) | Detached cue IDs/dirty state, insert/update/delete/commit failures and retry; passed. |
| [`os/tests/engine_staged_record_probe.cpp`](../os/tests/engine_staged_record_probe.cpp) | Real staged record writes, rollback and object state boundaries; passed. |
| [`os/tests/engine_track_publication_probe.cpp`](../os/tests/engine_track_publication_probe.cpp) | Accepted/stale/no-op record publication, retained cues/beats and 64 competing writer pairs; passed. |
| [`os/tests/scanner_app_driver.cpp`](../os/tests/scanner_app_driver.cpp) | Test-only scanner menu driver and injected database failure. |
| [`os/tests/scanner_commit_publication_probe.cpp`](../os/tests/scanner_commit_publication_probe.cpp) | Actual scanner finish slot and committed-track publication; passed. |
| [`os/tests/test_engine_apply_app.py`](../os/tests/test_engine_apply_app.py) | Real app acceptance runner with disposable profiles, database/media hashes and newly added disconnect mode. |
| [`os/tests/test_engine_import_package.py`](../os/tests/test_engine_import_package.py) | Compiles production validator with fast-math; 73 valid/invalid protocol cases passed. |
| [`os/tests/test_engine_import_state.py`](../os/tests/test_engine_import_state.py) | Compiles production merge/registry and tests actual schema41 SQL. |
| [`os/tests/test_engine_media_resolver.py`](../os/tests/test_engine_media_resolver.py) | Compiles production resolver; path traversal, symlink and retarget cases passed. |
| [`os/tests/test_engine_preview_app.py`](../os/tests/test_engine_preview_app.py) | Real menu/reader preview acceptance; verifies source unchanged and no imported tracks. |
| [`os/tests/test_engine_reader_service.py`](../os/tests/test_engine_reader_service.py) | Compiles production service; JSON corpus, limits, busy/cancel and descendant cleanup passed; 150-second timeout branch not exercised. |
| [`os/tests/test_scanner_app.py`](../os/tests/test_scanner_app.py) | Real recursive scanner acceptance; normal/repeat and rejected insertion cases, unchanged media and integrity. |
| [`os/tests/track_batch_commit_probe.cpp`](../os/tests/track_batch_commit_probe.cpp) | Completed-archive rollback/cache/retry probe; newly reproduced and fixed later-track autocommit leakage. |
| [`res/schema.xml`](../res/schema.xml) | Schemas 41/42: provenance registry and deletion invalidation; real migrations and reused-row identity cases passed. |
| [`src/database/mixxxdb.cpp`](../src/database/mixxxdb.cpp) | Advances the supported schema to 42; new and upgraded fixture databases tested. |
| [`src/database/schemamanager.cpp`](../src/database/schemamanager.cpp) | Multi-statement schema migration handling; schema and identity regression coverage. |
| [`src/library/dao/cuedao.cpp`](../src/library/dao/cuedao.cpp) | Detached staging, checked writes and commit-time cue identity/dirty-state acceptance; failure and retry probes passed. |
| [`src/library/dao/cuedao.h`](../src/library/dao/cuedao.h) | Staged cue and pending-save contracts; no claim of atomic live cue import. |
| [`src/library/dao/playlistdao.h`](../src/library/dao/playlistdao.h) | New staged playlist API and occurrence snapshot contract. |
| [`src/library/dao/playlistdao_import.cpp`](../src/library/dao/playlistdao_import.cpp) | Savepoint-protected playlist creation/update, preserved occurrence IDs, commit-only signals/cache; rollback and identity probes passed. |
| [`src/library/dao/trackdao.cpp`](../src/library/dao/trackdao.cpp) | Shared save and batch transaction boundary changes; review fixed autocommit leakage after implicit rollback. |
| [`src/library/dao/trackdao.h`](../src/library/dao/trackdao.h) | Pending track/record/cue state and committed-track output contract; paired with real archive probes. |
| [`src/library/engine/dlgengineimport.cpp`](../src/library/engine/dlgengineimport.cpp) | Table-model preview, helper discovery, cancellation, Apply and status UI; actual app tests. |
| [`src/library/engine/dlgengineimport.h`](../src/library/engine/dlgengineimport.h) | Table-model preview, helper discovery, cancellation, Apply and status UI; actual app tests. Header contract reviewed with implementation. |
| [`src/library/engine/enginecueimport.cpp`](../src/library/engine/enginecueimport.cpp) | Detached cue merge planner only; preserves banks, local controls and conflicts; not called by Apply. |
| [`src/library/engine/enginecueimport.h`](../src/library/engine/enginecueimport.h) | Detached cue merge planner only; preserves banks, local controls and conflicts; not called by Apply. Header contract reviewed with implementation. |
| [`src/library/engine/engineimportcoordinator.cpp`](../src/library/engine/engineimportcoordinator.cpp) | Real metadata/playlist Apply, per-item progress/cancel, media recheck and provenance; missing-drive exception fixed. |
| [`src/library/engine/engineimportcoordinator.h`](../src/library/engine/engineimportcoordinator.h) | Real metadata/playlist Apply, per-item progress/cancel, media recheck and provenance; missing-drive exception fixed. Header contract reviewed with implementation. |
| [`src/library/engine/engineimportmerge.cpp`](../src/library/engine/engineimportmerge.cpp) | Per-field baseline/local/source merge; conflicts retain local values and old baseline. |
| [`src/library/engine/engineimportmerge.h`](../src/library/engine/engineimportmerge.h) | Per-field baseline/local/source merge; conflicts retain local values and old baseline. Header contract reviewed with implementation. |
| [`src/library/engine/engineimportpackage.cpp`](../src/library/engine/engineimportpackage.cpp) | Strict typed protocol and graph validation; schema, identity, numeric and size limits. |
| [`src/library/engine/engineimportpackage.h`](../src/library/engine/engineimportpackage.h) | Strict typed protocol and graph validation; schema, identity, numeric and size limits. Header contract reviewed with implementation. |
| [`src/library/engine/engineimportregistry.cpp`](../src/library/engine/engineimportregistry.cpp) | Library-scoped source identity/baseline persistence; corrupt reads are not mistaken for first import. |
| [`src/library/engine/engineimportregistry.h`](../src/library/engine/engineimportregistry.h) | Library-scoped source identity/baseline persistence; corrupt reads are not mistaken for first import. Header contract reviewed with implementation. |
| [`src/library/engine/enginemediaresolver.cpp`](../src/library/engine/enginemediaresolver.cpp) | Canonical paths inside selected media root; no helper-provided path authority. |
| [`src/library/engine/enginemediaresolver.h`](../src/library/engine/enginemediaresolver.h) | Canonical paths inside selected media root; no helper-provided path authority. Header contract reviewed with implementation. |
| [`src/library/engine/enginemetadataimport.cpp`](../src/library/engine/enginemetadataimport.cpp) | Ordinary metadata mapping with deferred cue/grid/BPM timing; preserves local edits. |
| [`src/library/engine/enginemetadataimport.h`](../src/library/engine/enginemetadataimport.h) | Ordinary metadata mapping with deferred cue/grid/BPM timing; preserves local edits. Header contract reviewed with implementation. |
| [`src/library/engine/engineplaylistimport.cpp`](../src/library/engine/engineplaylistimport.cpp) | Occurrence-aware ordered playlist merge; local duplicates and edits survive repeat imports. |
| [`src/library/engine/engineplaylistimport.h`](../src/library/engine/engineplaylistimport.h) | Occurrence-aware ordered playlist merge; local duplicates and edits survive repeat imports. Header contract reviewed with implementation. |
| [`src/library/engine/enginereaderservice.cpp`](../src/library/engine/enginereaderservice.cpp) | Worker thread/process, strict JSON, output limits and process-group cancellation; no native firmware execution. |
| [`src/library/engine/enginereaderservice.h`](../src/library/engine/enginereaderservice.h) | Worker thread/process, strict JSON, output limits and process-group cancellation; no native firmware execution. Header contract reviewed with implementation. |
| [`src/library/scanner/libraryscanner.cpp`](../src/library/scanner/libraryscanner.cpp) | Publish newly scanned tracks only after commit; failure skips successful-scan cleanup. |
| [`src/library/trackcollection.cpp`](../src/library/trackcollection.cpp) | Propagates insertion/finish failure instead of returning an uncommitted ID. |
| [`src/library/trackcollection.h`](../src/library/trackcollection.h) | Friend access for two real save/purge regression tests added in review. |
| [`src/library/trackcollectionmanager.cpp`](../src/library/trackcollectionmanager.cpp) | Skips never-added tracks before export/save; preserves pending edits and actual purged-track behavior. |
| [`src/mixer/basetrackplayer.cpp`](../src/mixer/basetrackplayer.cpp) | Review follow-up: optional overview lookup no longer asserts when the skin control is absent; 16 cue-control tests passed. |
| [`src/mixxxapplication.cpp`](../src/mixxxapplication.cpp) | Allows the explicit Engine utility/file picker through kiosk dialog policy. |
| [`src/mixxxmainwindow.cpp`](../src/mixxxmainwindow.cpp) | Owns and opens the Engine dialog; exercised through the real menu. |
| [`src/mixxxmainwindow.h`](../src/mixxxmainwindow.h) | Declares the owned Engine dialog; lifetime paired with main-window integration. |
| [`src/test/trackdao_test.cpp`](../src/test/trackdao_test.cpp) | Never-added and actual purged-track save regressions; both passed. |
| [`src/track/track.cpp`](../src/track/track.cpp) | Conditional TrackRecord replacement under the existing mutex; stale/no-op and competing-publication probes passed. |
| [`src/track/track.h`](../src/track/track.h) | Updated/Unchanged/Stale result contract; separately mutable cues/beats are explicitly outside it. |
| [`src/util/db/sqltransaction.cpp`](../src/util/db/sqltransaction.cpp) | Detects implicit SQLite rollback using native connection state; dead transaction retry tests passed. |
| [`src/util/db/sqltransaction.h`](../src/util/db/sqltransaction.h) | Connection/thread and active-state interface for staged writes; native SQLite requirement retained. |
| [`src/widget/wmainmenubar.cpp`](../src/widget/wmainmenubar.cpp) | Library menu action; actual menu/preview tests passed. |
| [`src/widget/wmainmenubar.h`](../src/widget/wmainmenubar.h) | Signal declaration paired with menu and main-window connections. |
| [`tools/audio_timeline_probe.cpp`](../tools/audio_timeline_probe.cpp) | Actual provider PCM/range/seek diagnostic; review replaces unsafe fast-math std::isfinite with the project helper. |
| [`tools/engine-reader/CMakeLists.txt`](../tools/engine-reader/CMakeLists.txt) | Pinned dependency archive/hash and explicit local patches; fresh build and installed reader tested. |
| [`tools/engine-reader/README.md`](../tools/engine-reader/README.md) | Separate reader installation, supported schema and helper discovery instructions; firmware not required. |
| [`tools/engine-reader/diagnostics/README.md`](../tools/engine-reader/diagnostics/README.md) | Optional hash-specific emulation instructions and explicit limitations; separate from the installed public reader. |
| [`tools/engine-reader/diagnostics/aac-pns-findings.json`](../tools/engine-reader/diagnostics/aac-pns-findings.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/diagnostics/check-cue-reset.py`](../tools/engine-reader/diagnostics/check-cue-reset.py) | Optional bounded, hash-pinned native-runtime runner; reviewed statically and syntax-checked, not executed this pass. |
| [`tools/engine-reader/diagnostics/check-decoder-entry.py`](../tools/engine-reader/diagnostics/check-decoder-entry.py) | Optional bounded, hash-pinned native-runtime runner; reviewed statically and syntax-checked, not executed this pass. |
| [`tools/engine-reader/diagnostics/check-decoder-pcm.py`](../tools/engine-reader/diagnostics/check-decoder-pcm.py) | Optional bounded, hash-pinned native-runtime runner; reviewed statically and syntax-checked, not executed this pass. |
| [`tools/engine-reader/diagnostics/cue-reset-probe.c`](../tools/engine-reader/diagnostics/cue-reset-probe.c) | Optional native-runtime diagnostic shim; offsets and experimental behavior remain confined to explicit research runners, not application builds. |
| [`tools/engine-reader/diagnostics/decoder-entry-probe.c`](../tools/engine-reader/diagnostics/decoder-entry-probe.c) | Optional native-runtime diagnostic shim; offsets and experimental behavior remain confined to explicit research runners, not application builds. |
| [`tools/engine-reader/diagnostics/decoder-pcm-probe.c`](../tools/engine-reader/diagnostics/decoder-pcm-probe.c) | Optional native-runtime diagnostic shim; offsets and experimental behavior remain confined to explicit research runners, not application builds. |
| [`tools/engine-reader/diagnostics/long-mp3-findings.json`](../tools/engine-reader/diagnostics/long-mp3-findings.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/diagnostics/native-cue-load-policy.json`](../tools/engine-reader/diagnostics/native-cue-load-policy.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/diagnostics/native-cue-reset-results.json`](../tools/engine-reader/diagnostics/native-cue-reset-results.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/diagnostics/native-reader-expanded-results.json`](../tools/engine-reader/diagnostics/native-reader-expanded-results.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/diagnostics/native-reader-fixture-results.json`](../tools/engine-reader/diagnostics/native-reader-fixture-results.json) | Historical synthetic research result; parsed and checked scope/structure. Not independently rerun or treated as a production test in this review. |
| [`tools/engine-reader/engine-import.py`](../tools/engine-reader/engine-import.py) | Installed launcher entry point to bounded snapshot/package orchestration. |
| [`tools/engine-reader/engine-reader.cpp`](../tools/engine-reader/engine-reader.cpp) | Read-only libdjinterop metadata/cue/grid serialization on a temporary snapshot; installed tests passed. |
| [`tools/engine-reader/engine_import_package.py`](../tools/engine-reader/engine_import_package.py) | Read-only SQLite backup (including committed WAL), child execution, package assembly and cancellation. |
| [`tools/engine-reader/engine_limits.py`](../tools/engine-reader/engine_limits.py) | Central bounds for serialized data, counts and numeric domains. |
| [`tools/engine-reader/engine_media_path.py`](../tools/engine-reader/engine_media_path.py) | Canonical selected-root path resolution used in source package building. |
| [`tools/engine-reader/engine_playlist_manifest.py`](../tools/engine-reader/engine_playlist_manifest.py) | SQLite playlist hierarchy/order extraction; cycle/reference/identity validation. |
| [`tools/engine-reader/engine_reader_protocol.py`](../tools/engine-reader/engine_reader_protocol.py) | Typed native output parsing, finite numbers and bank/state checks. |
| [`tools/engine-reader/patches/libdjinterop-bounded-decompression.patch`](../tools/engine-reader/patches/libdjinterop-bounded-decompression.patch) | Bounds decompression/counts before allocating cue/loop/grid structures; dependency patch applied during fresh build. |
| [`tools/engine-reader/patches/libdjinterop-native115.patch`](../tools/engine-reader/patches/libdjinterop-native115.patch) | Raw default/adjusted main-cue state preservation; installed raw DB and app provenance tests. |
| [`tools/engine-reader/test_engine_import_limits.py`](../tools/engine-reader/test_engine_import_limits.py) | Payload/decompression-bound cases in standalone Python test run. |
| [`tools/engine-reader/test_engine_media_path.py`](../tools/engine-reader/test_engine_media_path.py) | Selected-root, foreign path and symlink cases in standalone Python run. |
| [`tools/engine-reader/test_engine_playlist_manifest.py`](../tools/engine-reader/test_engine_playlist_manifest.py) | Hierarchy, stable order, duplicate/reference and cycle fixtures in standalone Python run. |
| [`tools/engine-reader/test_engine_reader_protocol.py`](../tools/engine-reader/test_engine_reader_protocol.py) | Strict protocol numeric/type and bank fixtures in standalone Python run. |
| [`tools/engine-reader/test_engine_snapshot.py`](../tools/engine-reader/test_engine_snapshot.py) | Committed WAL included in snapshot without changing source database/WAL bytes. |
| [`tools/engine-reader/tests/check_cancellation.py`](../tools/engine-reader/tests/check_cancellation.py) | Real installed launcher child cancellation; passed. |
| [`tools/engine-reader/tests/check_metadata.py`](../tools/engine-reader/tests/check_metadata.py) | Real installed helper metadata fixtures; passed. |
| [`tools/engine-reader/tests/check_raw_metadata.py`](../tools/engine-reader/tests/check_raw_metadata.py) | Raw source SQLite semantics including main-cue state; passed. |
| [`tools/engine-reader/tests/create_metadata_fixture.cpp`](../tools/engine-reader/tests/create_metadata_fixture.cpp) | Opt-in synthetic dependency fixture generator; no installed profile/music access. |
| [`tools/engine-reader/tests/create_reimport_fixture.cpp`](../tools/engine-reader/tests/create_reimport_fixture.cpp) | Opt-in initial/update/maincue-state synthetic generator; optional 3.0.0/3.0.2 schema selection exercised. |
| [`tools/engine-reader/tests/prepare_reimport_fixture.py`](../tools/engine-reader/tests/prepare_reimport_fixture.py) | Creates synthetic audio and validated initial/update packages for persistence probe; passed. |
| [`os/tests/engine_large_library_app_driver.cpp`](../os/tests/engine_large_library_app_driver.cpp) | Real menu/preview/Apply test driver; captures GUI timer gaps without replacing importer code. |
| [`os/tests/test_engine_large_library_app.py`](../os/tests/test_engine_large_library_app.py) | Disposable larger-library generator/runner; exact count/order/provenance/hash/integrity checks; both accepted schemas passed. |
| [`src/test/coverartutils_test.cpp`](../src/test/coverartutils_test.cpp) | Corrected Opus fixture gate to require an actual provider rather than a MIME filename alias; full suite passed. |
| [`src/test/playermanagertest.cpp`](../src/test/playermanagertest.cpp) | Real preview deck setup plus 500-track import concurrent with two decoded deck buffers; all six tests passed. |
| [`src/test/samplerdrive_test.cpp`](../src/test/samplerdrive_test.cpp) | Constructs preview controls before Library/EDMC as CoreServices does; actual private-mount storage tests passed. |
| [`src/test/soundproxy_test.cpp`](../src/test/soundproxy_test.cpp) | Provider-specific first-sound references independently checked with FFmpeg CLI; full suite passed. |
| [`src/test/touchscrollfilter_test.cpp`](../src/test/touchscrollfilter_test.cpp) | Uses independently laid-out Qt hit target for the scroll/tap fixture instead of assuming a font-dependent row height. |
| [`src/test/wusblist_test.cpp`](../src/test/wusblist_test.cpp) | Tests offscreen row coordinates after scrolling; keeps the separate positive visible-row tap test. |
