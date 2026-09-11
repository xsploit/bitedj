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
grid and source loop, cue-only edits, 64 pairs of competing publications, and the
legacy setter. No profile, media file, deck, or Pi is opened.

## Apply integration still required

This primitive does not make database commit and live publication atomic. In
particular, a stale Track may already be **clean** because its newer edit was
saved before publication. The test covers clean-stale rejection; a coordinator
must not assume that every stale result is dirty or queued to save again.

The coordinator needs a database precondition/reconciliation policy as well as
this live-record check. It must surface failed persistence, keep local edits,
and define cancellation after a commit. Blindly committing SQL and relying on a
later incidental dirty save is insufficient. Cue timing, source-file matching,
new-track cache publication, external collections, and the overall Apply UI
remain separate work. No Engine cue alignment or real-time audio safety is
established by these Track-only tests.
