# Conditional publication to a live Track

`Track::replaceRecordIfUnchanged(expected, replacement)` compares TrackRecord
values and performs the existing replacement logic under the same Track mutex.
It returns `Updated`, `Unchanged`, or `Stale`. A stale result makes no record,
beat-grid, dirty-state or signal change. The original `replaceRecord` API keeps
its existing bool behavior and uses the same implementation.

The comparison detects changed values, not an edit history: changing a field
and changing it back is not a stale value. It does not snapshot separately mutable
cues or beat-grid contents. No database work runs under the Track lock.

## Validation

```sh
python3 os/tests/test_engine_cue_origin.py /path/to/build \
  --ninja /path/to/ninja \
  --probe-source "$PWD/os/tests/engine_track_publication_probe.cpp"
```

The completed-archive probe uses actual Track objects and the Engine metadata
planner. It covers accepted publication, clean no-op, stale rejection after an
edit on another thread, signal/dirty-state behavior, preservation of a real beat
grid and source loop even with a conflicting incoming Engine BPM, cue-only edits, 64 pairs of competing publications, and the
legacy setter. No profile, media file, deck, or Pi is opened.

## Integration limits

This primitive does not make database commit and live publication atomic. In
particular, a stale Track may already be **clean** because its newer edit was
saved before publication. The test covers clean-stale rejection; a coordinator
must not assume that every stale result is dirty or queued to save again.

The metadata/playlist coordinator now uses this live-record check before the
ordinary save path and records provenance separately after saving. Its actual
application tests and partial-failure/cancellation behavior are documented in
`ENGINE_APPLY.md`. `TrackCollectionManager::saveTrack` skips clean tracks before
calling the DAO, so an unchanged reimport does not violate the DAO's dirty-track
precondition. The metadata planner starts from the current record and does not
adopt incoming Engine BPM; the conflicting-BPM publication test verifies that
the existing beat object and bytes remain unchanged.

This is not atomic publication of separately mutable cues/beat contents with
SQL. Safe live cue publication, source timing alignment, external-collection
behavior and real-time audio safety remain separate requirements. Blindly
committing SQL and relying on a later incidental dirty save is insufficient.
