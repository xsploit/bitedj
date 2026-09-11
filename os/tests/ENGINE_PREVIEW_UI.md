# Engine library preview (draft import workflow)

The Library menu opens **Preview Engine DJ library…**. Choose a drive containing `Engine Library/Database2/m.db`, or the Engine Library folder itself. The asynchronous reader shows track titles/artists, lengths, populated hot-cue/loop counts and reported media availability. Reading can be cancelled; rejecting or closing the dialog cancels the request too. Large libraries use a table model rather than allocating a widget for every track. Rows/buttons have 44-pixel minimum touch targets and the table supports touch scrolling.

This is a read-only preview. It does not add tracks, copy music, apply cues or update playlists. The import coordinator, conflict decisions, independently revalidated media access and final publication still need implementation. Reported media availability is a reader observation, not permission to open a file or proof of decode compatibility.

The kiosk policy permits this explicit utility and its child file picker, as it already permits Preferences. Other dialogs remain suppressed. Physical touch/FLX6/Pi testing is pending; the Pi was not used for this change.

## Reader installation

Build/install the separate reader component as described in `tools/engine-reader/README.md`. For an app installed in `PREFIX/bin`, the dialog finds `PREFIX/libexec/bitedj-engine/engine-import.py`. An adjacent `libexec/bitedj-engine` directory is also supported. Development builds can set `BITEDJ_ENGINE_IMPORT_HELPER` to the absolute path of the installed executable launcher. The application does not silently download or install a helper.

## Full application regression

Build the app with Engine support and the reader’s explicit `engine-reimport-fixture` target, then run:

```sh
python3 os/tests/test_engine_preview_app.py \
  --build /path/to/app-build \
  --reader-prefix /path/to/reader-stage \
  --fixture-generator /path/to/reader-build/engine-reimport-fixture \
  --output /path/to/test-output
```

Requires Linux, a C++ compiler, Qt6Widgets development files, Python and the completed app/reader builds. It creates a synthetic Original/VIP Engine database, one synthetic WAV and one intentionally missing WAV. The test-only preload drives the actual Library menu, invokes the folder-selection slot, verifies table/summary/media states, checks invalid selection and absent helper behavior, cancels a slow reader by rejecting the dialog, and successfully reopens the library. The preload is removed from the subprocess environment so the real reader is not instrumented. It verifies unchanged source DB bytes and zero imported tracks, and saves the dialog screenshot and logs. No installed profile, firmware, real music, physical file-picker gesture or Pi is required.

The screenshot and successful checks establish an offscreen Qt workflow, not native display performance or a complete Engine import.
