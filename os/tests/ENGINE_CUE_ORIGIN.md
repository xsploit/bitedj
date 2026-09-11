# Engine cue origin persistence regression

The runner and C++ probe are stored together in `os/tests/`.

After building BiteDJ with Ninja and `CMAKE_EXPORT_COMPILE_COMMANDS=ON`:

```sh
python3 os/tests/test_engine_cue_origin.py /path/to/build
```

Use `--ninja /path/to/ninja` if Ninja is not on PATH. The runner reuses the existing build's compile flags and link dependencies; it does not configure or rebuild the app. It writes its probe objects/executable and synthetic SQLite database into temporary directories, with a 30-second probe timeout. No music, firmware, installed profiles, or private paths are required.

The probe calls the actual CueDAO/Cue code and verifies separate local cue/loop indices with the same Engine source slot, fractional positions, source identity after reload and label/color edits, no dirty state from an unchanged setter, origin-less cues, and deletion.

`--fresh-cues` compiles current `cue.cpp` and `cuedao.cpp` into separate objects ahead of the existing archive. This diagnostic mode can run during a larger rebuild but is not evidence that the full current app builds or starts. Default mode tests the completed application archive. This runner currently targets Linux Ninja builds; Windows packaging and full-app restart/touchscreen behavior are separate gates. The probe directly creates the expected cue table, so it does not replace the schema-migration regression test.

## Integration evidence

The current implementation also passed an isolated real SchemaManager migration from version39 to40, preserving eight existing cue rows and leaving source identity NULL. Two full application restart cycles retained seeded hot cue1 and loop1 with fractional positions and an ordinary local cue. Normal export retained both Engine banks at source slot1. Four real export-job conflict tests (hot cue/loop, source-first/local-first) emitted the expected failure rather than silently overwriting a destination; no Track rows were written for those single-track tests, though audio was copied before the conflict was detected.

These integration results are separate from the portable probe. Full Engine import, source merge/idempotence, saved-loop UI access and Windows runtime remain pending. Existing startup control and EGL warnings were observed during isolated application tests.

## Edited cue types and source banks

A local edit can turn an imported saved loop into a jump cue while retaining its source bank/slot. Export now rejects a cue whose type no longer matches that bank, rather than silently skipping it or moving it into a possibly occupied bank. Restore the original cue/loop type before export. An unlabeled cue is identified by its original bank and slot in the error message.

The production-block regression covers both mismatch directions and unlabeled errors. Actual application export tests independently observed both errors, zero destination Track rows for the single-track failures, and successful export of valid matching banks. Other previously exported tracks or copied audio may remain after failure.
