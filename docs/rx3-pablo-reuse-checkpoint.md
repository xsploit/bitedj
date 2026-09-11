# RX3 graphics and Pablo fork reuse checkpoint

This checkpoint preserves the useful findings from the RX3/Engine research and
comparison with [Pablo’s Custom Bite DJ](https://github.com/pablo-feijo/custom-bitedj).
It is not a completed waveform-preview or RX3-skin port.

## Implemented here

Add two non-unique indexes to BiteDJ’s internal Rekordbox tables:

- `(rb_id, device)` for exported-track lookup.
- `(playlist_id, position)` for ordered playlist reads.

Adapted from Pablo’s release 0.0.7, commit
`c1a3fb4` (see the full revision in the PR description). Preserve this fork’s
`.PIONEER` discovery and non-unique analysis paths; copying Pablo’s entire
reader would lose those fixes. No exported USB database is modified by these
indexes: they belong to BiteDJ’s internal tables.

`python os/tests/test_rekordbox_indexes.py` compiles the production table-creation
functions with Qt6Core/Qt6Sql and exercises SQLite with 20,000 tracks. It checks
indexed lookup, ordered duplicate playlist entries without a temporary sort,
shared analysis paths, duplicate-location rejection and repeated initialization
of an existing table missing its index. It requires a C++20 compiler,
`pkg-config`, Qt6 development packages and the QSQLITE driver.

## Next: playlist waveform thumbnails

Pablo implements thumbnails in
`src/library/tabledelegates/previewbuttondelegate.{h,cpp}`, supported by
`src/waveform/renderers/waveformpreviewrenderer.{h,cpp}` and
`src/test/waveformpreview_test.cpp`.

Useful behavior includes background summary loading, 128-entry caches,
location-keyed results that survive sorting, visible-row refreshes and
invalidation when analysis or display settings change. Existing summaries are
required; this does not promise instant waveforms for unanalyzed tracks.

Integration must preserve our audition behavior: Pablo replaces the current
preview-play button/editor. His implementation also needs added interfaces such
as `lookupPublishedTrackByLocation` and `previewDbConnectionPool`, plus build
registration. Do not copy the delegate alone.

- [ ] Port the renderer and dependency interfaces.
- [ ] Add asynchronous thumbnails while preserving a separate audition action.
- [ ] Port/adapt tests for cache limits, sorting, progress and palette changes.
- [ ] Test missing/slow USB media and large playlists on the Pi.
- [ ] Verify the full application build and live browsing before deployment.

PSSI phrase and PWV6/PWV7 import already exist in this fork. Avoid duplicating
that work while importing the preview improvements.

## RX3 graphics extraction result

The existing Pi filesystem backup contains
`root/gui/pset/imagedata/imagedata.dat`, 40,762,695 bytes. Its 245,564-byte table
contains 5,581 records of 44 bytes. The observed width/height and data-offset
fields describe consecutive two-byte-per-pixel images. Little-endian RGB565
decoding produced 5,581 PNGs; sample inspection showed a background and BPM
button. Every pixel range fits consecutively; 7,707 trailing bytes remain
uninterpreted. All generated PNG chunk CRCs passed.

These are graphics, not a complete BiteDJ skin. Widget identifiers, screen
layouts, control bindings and transparency semantics are not recovered.
Magenta/color-key pixels were retained. No proprietary graphics, firmware,
keys or user library data are included in this repository or PR. Use the local
extraction as visual reference and establish asset rights before distribution.
The earlier separate updater PNGs contain only firmware-status messages.

- [ ] Design the desired layout with BiteDJ XML/QSS and existing controls.
- [ ] Recreate distributable assets or establish their reuse rights.
- [ ] Preserve FLX6 mappings, existing layout choices and touch target sizes.

## Validation and handoff boundary

The source base was `4c1dfec590` on xsploit/bitedj main. The Windows PiFlex
checkout matched it, with separate local OBS-overlay changes. That volume was
read-only, so it was not edited. The index patch passed an apply check against
that checkout. No firmware, Pi runtime or installed BiteDJ binary was changed.
The component test passed; a full application build and Pi performance
benchmark have not been performed for this change.

## Follow-up: preview behavior from RX3 1.20 static analysis

A focused local Ghidra pass recovered approximate pseudocode for fifteen preview
functions. The stock 1.20 player differs from the inspected 1.19 player by only
three bytes, so this does not imply a new preview feature in 1.20.

Observed behavior includes message-based preview requests, a loaded-state guard
before seeking, a separate preview channel, and attenuator fade commands. The
ready-path fractional seek routine suppresses changes smaller than approximately
0.03 and adjusts positions near the end. These are static observations, not live
behavior measurements; uncertain decompiler float signatures were checked against
ARM instructions where used. No proprietary pseudocode is included here.

BiteDJ already has asynchronous loading and stale-completion rejection. For a
combined thumbnail/audition feature, preserve the current preview play button,
associate pending seeks with track identity, retain the last drag position, and
apply it only after that same track loads. Coalesce seeks without copying RX3's
exact 3% threshold. Test rapid track switching, sorting during loading, media
removal, final drag release and headphone routing. This follow-up records a design
target; it does not implement or validate the new UI.

## Whole-player map and library-analysis follow-up

The local RX3 1.20 research now includes an offline searchable map combining
18,296 function entry addresses, 75,519 static call references and 745 retained
source filenames. Category assignments are heuristic; indirect calls, message
queues, external libraries and some tail calls remain incomplete. This maps the
player executable, not the entire firmware or an original buildable source tree.

Eleven more library/analysis routines were decompiled locally. `DBSA_SongAnalyze`
queues a message via `SendMsgToDBSA`; it is not an extracted BPM/key detector.
`DBAnalyzeInit` initializes database/traversal state. The inspected
`DiscDB_AnlzMusicFile` is a stub. `MAnlz_WriteQTZ` serializes quantize/beat records,
while `MAnlz_ReadAnlzFile` also reaches writers in some branches. Before importing
behavior, distinguish read-only parsing from mutation and compare against real
fixture files. No claim of a complete standalone/bulk analyzer is justified yet.

Next research target: follow the DBSA message dispatch into workers, then compare
library identity, playlist hierarchy and ANLZ/cue behavior with BiteDJ's existing
implementation. Original firmware, pseudocode and the binary map remain outside
this repository; this checkpoint contains only findings and implementation goals.

### Broader static pass

The next pass exported 17,376 non-external player functions as approximate local
pseudocode, recovered 21 initially missed symbol ranges and expanded the static
call map to 86,742 references. Two decompiler failures remain. Export completion
is not semantic validation; 1,383 outputs carry warnings.

Actual audio-block processing exists separately in `BpmWaveDetectManager` and
`BpmWaveDetect`, including BPM checking and waveform generation. The database
song-analysis worker routes to container/tag parsers. One misleading interface,
`MAnlz_LoadKey`, is an ARM return-zero stub in this build. Next investigation:
resolve the detector's indirect filter stages and sample/state units before any
comparison with BiteDJ's existing beat/key/waveform analyzers. Do not infer a
complete offline analysis suite or better detection quality from exported names.

### Filter verification and export recovery

Both detector filter objects resolve through their vtables to a four-lane
second-order IIR leaf. Executing the actual ARM leaf in isolated emulation matched
an independent difference-equation model in 21 synthetic cases. Partitioning input
changed output by at most about 6e-8 in the tested sequence; it was not bit-identical.
This validates arithmetic/layout for chosen coefficients, not BPM accuracy or speed.

The stock audio startup initializes detector/effect processing at 44.1 kHz even
while querying the device rate. Preserve explicit rate conversion in porting work;
do not infer correct internal timing merely from opening a different-rate device.

Both previously failed function exports were recovered using different decompiler
settings. There are now 17,378 non-external pseudocode exports, but semantic and
runtime validation remain limited. No proprietary pseudocode is included here.

### Detector frequency paths verified

Executing the stock detector constructor in isolated ARM emulation recovered its
actual filter configuration. The two stages form three frequency paths plus an
identity lane. At the stock 44.1 kHz assumption, the cascade selects a narrow bass
band around 150 Hz, a broad middle band and a high-frequency band. The inspected
BPM routine sends the three filtered lanes to peak/interval checking.

The original ARM filter leaf, executed twice with the recovered configuration,
matched calculated sine gains in all 28 tested frequency/lane combinations;
maximum absolute gain error was about 6.55e-6. This verifies the frequency front
end, not complete beat detection or better musical results. The constructor test
stubs allocation/memset and does not boot the firmware or validate live hardware.

BiteDJ research candidate: compare independently implemented multiband onset
features against its existing analyzers on controlled clicks and a labeled music
corpus. Preserve rate conversion, half/double-tempo handling and confidence in the
evaluation. No recovered coefficient table, firmware or proprietary pseudocode is
included in this repository. Full peak/interval semantics remain under analysis.
