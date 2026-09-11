# Engine metadata planning against TrackRecord

`planEngineMetadataImport` copies a current TrackRecord, translates fields from an already validated Engine track and applies the three-way merge to that copy. It returns the proposed record, accepted baseline, changed fields, conflicts and values that cannot be represented. It does not alter a live Track, emit model/audio signals, open a transaction or export tags.

Run against a completed Linux build:

```
python os/tests/test_engine_cue_origin.py /absolute/build --ninja /path/to/ninja --probe-source /absolute/repo/os/tests/engine_metadata_import_probe.cpp
```

The test links the actual completed application archive. It compares every Engine key ordinal against BiteDJ's parsing of the corresponding musical name, tests all six rating levels, and checks initial/repeated import, local-only edits, concurrent conflicts, independent source updates, repeat conflicts, absent sourceTitle, managed title clearing, unowned title protection, unrepresentable ratings and custom local key text. Identity, date added, main cue, BPM lock, stream properties, album artist, grouping and track total remain unchanged. The input record is unchanged.

Rules:

- Baselines contain normalized BiteDJ values. Musical keys use BiteDJ enum values after explicit circle-of-fifths translation; an unknown nonempty local key label is retained as text so it cannot be mistaken for an empty key.
- Missing optional fields are unmanaged. `sourceTitle` is the source field; legacy `title` is only a display fallback. Source null strings normalize to empty strings, so a managed source clear can apply while an unowned local title is protected by a conflict.
- An unowned empty field can be initialized. An existing nonempty field must agree or produce a conflict. `newEntity=true` permits initialization over file-tag values; the caller must establish that the entity is actually new.
- Rating percentages divisible by20 map exactly to0–5 stars. Other percentages are reported as unrepresentable and do not advance their baseline; no silent rounding.
- Publisher maps to record label only with extra metadata support; otherwise non-null publisher is reported as unrepresentable. This test proves the record projection, not persistence of every extra metadata field.
- Only accepted changed fields are assigned. An unchanged key does not rebuild unrelated key analysis state.
- Audio properties, byte counts, BPM/beat grids, main cue and cue banks are outside this metadata plan. Their coordinate and storage policies require separate staging.

Integration blocker found in the existing save path: TrackDAO add/update own their transactions; saveTrack marks live data clean, cue saves assign IDs/clean state, and the cue save result is not propagated. Analysis and override side effects also exist. An outer transaction around existing getOrAddTrack/saveTrack cannot provide atomic import rollback or postcommit cache publication. A transaction-aware staged write interface is still required, followed by source-identified cue/playlist application, deletion conflicts, model publication and user-facing import/bank controls. This planner does not complete the Engine import feature.
