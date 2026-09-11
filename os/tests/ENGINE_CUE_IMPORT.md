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

The generator produces two distinct playable 3-second stereo PCM WAVs at44.1kHz and Engine schema3.0.2 libraries with stable source identities. The persistence probe validates both packages, creates the actual BiteDJ schema0→41 and stages the planner output through production CueDAO and EngineImportRegistry. Initial import, rolled-back update, committed update, repeated import and reload preserve IDs, separate hot1/loop1, fractional frames and loop8 ending at132300. Its Cue materialization/snapshot adapter is test code with detached DAO objects; it does not establish safe publication to loaded player objects. Native Engine duplicate playlist membership is separately represented as a protocol-only fixture, because the generated native schema forbids it.
