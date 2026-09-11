# Engine metadata and playlist import (draft)

Open **Library → Preview Engine DJ library…**, select the Engine Library or its drive, review the preview, then choose **Import metadata + playlists**. Preview alone remains read-only. Install the separate reader using `tools/engine-reader/README.md`; an app in `PREFIX/bin` discovers `PREFIX/libexec/bitedj-engine/engine-import.py`. Apply requires a native SQLite-enabled build and schema42.

## What is applied

Supported track metadata uses accepted source baselines to preserve local edits and report conflicts. Source musical keys are translated to BiteDJ keys. Playlists retain ordered occurrence identities, including local duplicate songs. Engine hierarchy becomes names such as `Parent / Child`. Name collisions receive a suffix; source-name mapping preserves that suffix on repeat imports and follows later source renames without claiming unrelated local playlists.

Missing, unreadable, outside-root or size-mismatched media is skipped. Paths are resolved again at actual import. A playlist with any unresolved track is deferred as a whole; tracks are never silently omitted from its membership. Path and file-size checks do not establish decoded audio identity.

Engine cues, loops, BPM and beat grids are **not applied**. Their source values are retained in provenance for later work. Engine star ratings on removable media are also deferred: existing portable rating overrides, including explicit zero ratings, must remain authoritative. Internal-media ratings can be imported. Waveforms, artwork and stems are not imported by this workflow.

## Saving, cancellation and conflicts

This is not one atomic transaction for an entire library. Each track is published as an ordinary metadata edit and saved through the normal collection manager **before** its provenance is recorded. Normal tag-writing preferences apply. A failed metadata save leaves the visible edit unsaved and does not accept its source baseline. If saving succeeds but provenance fails, the saved edit remains and a notice requests a retry.

Each playlist and its source bindings commit together, with cache publication after commit. Cancellation is checked between items; completed changes remain. Large individual items can still occupy the UI thread. No large-library responsiveness or concurrent playback performance claim is made.

When both local and source values change, local edits are retained and listed for review. There is no take-source conflict button yet. Deleted or invalidated local bindings require review before restoration. Source playlist disappearance does not automatically delete local playlists. Schema42 invalidates deleted playlist/occurrence bindings transactionally to prevent reused SQLite row IDs from claiming replacement content.

## Actual-app regression

Build the complete application, install the reader, and build its `engine-reimport-fixture` target. Then run:

```sh
python3 os/tests/test_engine_apply_app.py \
  --build /path/to/app-build \
  --reader-prefix /path/to/reader-stage \
  --fixture-generator /path/to/reader-build/engine-reimport-fixture \
  --output /path/to/results
```

The Linux Qt preload test uses temporary profiles and synthetic WAV files. It covers initial import, repeat import, local title conflict with a source metadata update, preservation of a local duplicate, cancellation, missing media, injected database-save failure, name collision, collision reimport and later source rename. It checks source-database and default-preference media hashes, SQLite integrity, row identities, and absence of applied Engine-origin cues. It writes logs, screenshots and a result JSON with the tested binary hash.

Run `test_engine_preview_app.py` with the same arguments plus `--installed-reader` to test normal helper discovery without a development override and confirm that preview alone adds no tracks.

For the portable-rating path, run the same test inside an isolated mount namespace with `--portable-mount /mnt/usbtest`. The option requires an empty disposable mount, writes a four-star override and an explicit zero-star override, and checks their application, database contents and unchanged update markers across every case. It also checks that deferred source ratings are not accepted into the metadata baseline.

```sh
unshare -Umr --propagation private bash -c '
  mount -t tmpfs tmpfs /mnt &&
  mkdir -p /mnt/usbtest &&
  mount -t tmpfs tmpfs /mnt/usbtest &&
  python3 os/tests/test_engine_apply_app.py \
    --build /path/to/app-build \
    --reader-prefix /path/to/reader-stage \
    --fixture-generator /path/to/reader-build/engine-reimport-fixture \
    --portable-mount /mnt/usbtest \
    --output /path/to/results
'
```

The mounts exist only in the private namespace. Do not pass a real USB drive to this fixture generator. A tmpfs test exercises the app's removable-storage code path, not physical USB behavior.

These desktop tests do not establish physical USB removal behavior, Windows runtime behavior, Pi touch interaction, FLX6 mapping or audio performance. The Pi remains off during this work.

Run the same command with `--media-recheck-only` for two additional full-app cases: a synthetic audio file whose byte size differs from the Engine record, and a same-content symlink that resolves outside the selected drive. Each case must import only the unaffected track and defer both playlists without omitting entries. The checks also verify source/media hashes and SQLite integrity. These cases start with changed media before preview; they do not simulate an eject or a change between preview and Apply.

Use `--main-cue-state-only` for the full-app default/adjusted main-cue regression. It generates one track with a nonzero default and zero unadjusted cue, and another with a nonzero adjusted cue, checks exact `mainCueState` preservation in deferred timing provenance, and repeats the import to verify unchanged local content and source state. Source/media hashes and the absence of applied Engine-origin cues are checked in both runs. The fixture generator's `maincue-state` action is for disposable test libraries only.
