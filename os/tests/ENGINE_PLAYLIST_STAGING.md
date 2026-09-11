# Engine playlist staging

PlaylistDAO now has stageNewPlaylist, stagePlaylistUpdate and commitStagedPlaylists for importing visible playlists. Staging shares the caller SqlTransaction, uses a savepoint, checks referenced tracks, preserves ordered duplicate occurrences with separate returned entry IDs, and does not change membership caches or emit signals. Engine playlist/entry provenance can be written in the same transaction with those IDs.

commitStagedPlaylists validates the staged playlist names, visibility, entry IDs, track IDs and order, then commits the caller's entire transaction. Only after a successful commit does it fill membership caches and emit notifications. It populates all supplied playlist caches before the first signal. Failed validation leaves the transaction active for caller rollback; failed commit emits nothing. Already completed transactions cannot publish again. These methods require the DAO thread and connection.

This API stages new playlists and exact-snapshot updates of existing playlists. Existing-playlist three-way source/local merge, source deletion/reorder policy, folder mapping, live Track publication, import preview/conflicts UI and the overall import coordinator remain unfinished. The caller must stage every intended write before calling commitStagedPlaylists: it commits the whole transaction, including track/cue/provenance writes, not just playlists. Staged values are an internal caller contract, not an authorization boundary.

Validation: full application build passed. The completed-archive probe used isolated in-memory SQLite with foreign keys enabled and the columns used by these APIs. It verifies silent rollback, stable occurrence identity for ordered42/43/42, cache readiness at notification, commit-only signals, provenance commit/rollback through EngineImportRegistry, double-publication rejection, changed-row rejection, missing-track rejection, mid-insert trigger failure with savepoint rollback preserving earlier staged work, deferred-foreign-key commit failure without publication, and integrity_check. The version smoke test passed. This is not full-schema migration, running UI or concurrent deck validation.

Run the existing os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_playlist_staging_probe.cpp. No fresh-source substitution was used for the passing archive probe.

Existing updates require an exact prior name and ordered occurrence ID/track snapshot. Locked or hidden playlists, stale snapshots, missing tracks and foreign/duplicate occurrence IDs are rejected. Retained occurrence IDs survive reorder; zero desired IDs create new occurrences. New rows are inserted before old rows are removed so a deleted highest ID cannot be recycled within that update. Savepoint rollback restores partial updates. Publication refreshes memberships and emits rename/content notifications only after commit.

Additional actual-archive tests passed for existing reorder/rename, stable occurrence IDs, locked and stale rejection, foreign ID rejection, partial-update trigger rollback and retry, update publication and highest-occurrence replacement. The ordered readback uses PlaylistTracks ORDER BY position,id. PlaylistDAO::getTrackIds uses DISTINCT and is not an ordered occurrence snapshot API. The initial test incorrectly used it; correcting the readback confirmed the stored order without changing that existing API.

The full application rebuild and version smoke check passed. The complete Engine import coordinator, conflict decisions and UI remain unfinished. This is a persistence layer, not a completed end-user import workflow.

Transaction-abort regression: a trigger raising ROLLBACK ends SQLite's outer
transaction. The transaction wrapper must then reject staging retries, commit
and rollback; a new transaction must still stage and roll back normally. A dead
wrapper must not become active again when that new transaction starts. This is
distinct from the existing RAISE(ABORT) savepoint-recovery cases, which retain
the outer transaction and permit retry. Native state checking uses the existing
`__SQLITE3__` / LOCALECOMPARE Qt/native-SQLite linkage contract. Builds without
that integration retain cached transaction-state behavior; this regression is
not claimed fixed for those configurations, and importer integration there
remains blocked pending equivalent transaction-state handling.

With native SQLite enabled, the full Linux application rebuild and expanded
completed-archive playlist probe pass. An isolated actual-app metadata/BPM save
and restart also pass, preserving eight cue rows/source identities and the
updated beat blob across restart. No installed profile or Pi was changed.
