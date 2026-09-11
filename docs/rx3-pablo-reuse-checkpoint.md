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

### Executed tempo path and confidence separation

The isolated stock ARM detector now processes controlled stereo audio through its
actual global initializer, constructor, init, filters and tempo routines. Six
30-second pulse fixtures (90/120/128/174 BPM mixed tones, plus bass/noise at 120)
finished within 0.02 BPM of the input. Silence kept detection unset. No music
corpus, live hardware or comparison with BiteDJ has been validated.

Actual provider/manager getter tests confirm tempo and validity are separate:
the default 12000 (BPM times 100) remains readable while detection is false.
Validity checks a separate detector byte and manager/unit gates. Keep confidence
and lifecycle state separate from a plausible numeric BPM in any analyzer UI.

A further limitation is now proven: the default checkBeat pseudocode removes
35 block addresses that executed in the 128 BPM fixture. Two decompiler-settings
retries remain wrong. Export success is not correctness; use the original ARM
behavior for reference until the reconstruction is repaired. No proprietary
source, firmware or coefficient tables are included in this repository.

### Independent reconstruction defect reproduction

The checkBeat control-flow loss reproduces in a small independently written ARM
fixture with no RX3 code. Ghidra 12.1.3 turns the conditional-move example into a
constant return, contradicting three positive finite inputs executed in Unicorn.
A simpler comparison control behaves correctly; a fresh RX3 import still shows
the original loss. Predicate simplification disabled does not fix it. The exact
backend cause remains open, so no corrected source export is claimed. A narrow
player-wide scan finds this exact six-instruction pattern only in checkBeat; that
is not a correctness audit of the other functions.

### Playlist ordering fix implemented

The PDB importer stored exported sort keys in QMaps but then visited guessed
consecutive indexes with operator[], inserting missing rows while using the
changing map size as its loop bound. Sparse keys caused unnecessary work and map
mutation; a zero-position track entry could keep extending the loop.

Playlist construction now iterates actual keys in sorted order, treats all input
maps as const, and preserves unsigned positions in SQLite through qint64. The
standalone tools/test_rekordbox_playlist_import.py compiles the two production
functions against Qt/SQLite. The original importer fails its sparse-key fixture;
the fix passes sparse/nested/empty playlists, reimport, missing references,
device-scoped track identity, zero/large positions and input-map preservation.
TreeItem is a test double; this does not replace a full application/PDB import test.

The separate RX3 mapping confirms rowset-based playlist and membership traversal,
content lookup, cancellation checks and explicit sorting in selected modes.
Internal table names include djdbPlaylist, djdbSongPlaylist and djdbContent.
These static findings do not establish direct exported-format equivalence or
complete OneLibrary support. No proprietary code was used for the BiteDJ fix.

### Supporting component inventory

The wider RX3 inventory now verifies 311 ELF occurrences representing 262 unique
contents and records their symbols plus 830 declared dependency edges. This is
inventory coverage, not complete source recovery. All 19 direct player libraries
have candidates; 463 of 465 dynamic imports have symbol-name matches, with the two
unmatched entries weak. Loader precedence, symbol-version compatibility and actual
runtime plugin loading remain unverified. MP3/FLAC decoding, ALSA I/O, g2d/DirectFB
graphics and JPEG/text conversion are separately identifiable dependency boundaries
for further source matching and porting research. No supporting firmware binaries
are added to this repository.

### Startup and loader evidence

Rootfs startup scripts launch rbp without a loader-path override in the inspected
chain. Framebuffer graphics aliases and actual rootfs paths narrow dependency
selection to 22 contents including the player/interpreter. A stock ARM loader
trace under QEMU in a read-only, network-isolated synthetic root exited 0; every
listed dependency file matched the profile's hash. This is a loader-only check,
not player startup or hardware validation. The update script's library-copy phase
is separate from normal startup. Installed pkg-config version declarations provide
source-matching leads but are not yet proven binary build identities.

### Detector edge cases and stereo comparison

Fourteen additional synthetic audio scenarios executed the stock RX3 detector in
bounded ARM emulation. On the tested pulses, 60/65 BPM doubled, 200 BPM halved,
and 181/182/184/187 BPM did not detect within 30 seconds. This is fixture-specific
behavior, not a universal supported-tempo range or real-music accuracy score.
A 90-to-128 BPM transition took about 8.45 seconds to acquire the new estimate.
After a pulse train stopped, the historical BPM flag stayed set while current
peak availability cleared. Nine actual ARM getter cases independently verified
the peak-availability gate and the recent-history adjustment boundary.

An independently written C++ probe compiled the actual BiteDJ
DownmixAndOverlapHelper implementation. Equal/opposite stereo channels cancelled
completely in its mono output. This helper is used by Queen Mary analysis;
this was not a full Queen Mary BPM test. BiteDJ's separate bass-energy path already
squares independently filtered channels and avoids this cancellation mechanism.
No analyzer change is justified by these synthetic fixtures alone, particularly
because the downmix helper also serves key analysis. Next evaluation should include
real music, stereo phase variants, tempo changes and half/double-tempo ambiguity,
and distinguish retained tempo from current beat timing. Raw firmware and
proprietary pseudocode remain outside this repository.

### Verified RX3 image-ID lookup

The actual RX3 1.20 ARM lookup accepted every one of the 5,581 image records from
the earlier local Pi backup. The numeric ID is the zero-based 44-byte record
index; pixel pointer relocation and two-byte row strides matched all records.
Three invalid IDs returned null. Seven synthetic format cases additionally tested
stride selection and conditional auxiliary-pointer relocation. The accessor
modifies the in-memory record, so it is not a purely read-only table lookup.

Static tracing connects these IDs to glyph properties, dimensions and drawing.
This provides a verified bridge from numbered extracted images to UI references,
but semantic widget names, states, transparency and full rendering remain open.
The backup is structurally compatible with this lookup; it is not proven to be
the asset payload paired with firmware 1.20. No vendor images or firmware code
are included here. Recreated BiteDJ assets still need independent layouts and
control bindings rather than treating extracted PNGs as a complete skin.

### Hot-cue controls mapped to image families

The stock hot-cue selection function passed 784 controlled ARM scenarios with
UI lookup/visibility/refresh/image sinks stubbed. Its named tables map two decks
of eight object IDs to shared image families: two banks, eight pads, 65 color
slots, plus loop overlays. There are 1,089 unique referenced image IDs. Tests
cover both color sources, every color slot, selected-pad values 0/1/8, and empty
or unhandled cue states. Color zero maps to slot nine; empty cues use slot 63.
One bank visually adds a cyan border in the inspected pad-A sample, but the
producer of the bank-selection state is not yet mapped to a user action.

These findings support independently composed cue occupancy, color, emphasis and
loop-marker states in a BiteDJ layout. They do not establish complete event
behavior, rendering, or a finished skin. Vendor artwork and extracted tables
remain in local research artifacts; none are committed here.

### Hot-cue state source and palette behavior

Static producer tracing, cross-checked against ARM stores, identifies the
standalone highlight inputs as gate-playing and current loop kind. Empty cue
state comes from the absent IN-time sentinel; loop markers come from the hot-cue
loop flag. This is not yet an end-to-end engine-to-screen test, and HID behavior
remains separate. The color converter was executed for 64 stock keys, all 256
possible output bytes and a missing key. It normalizes slots 46–48 to 45 by
mutating the matched table entry. BiteDJ can keep occupancy, color and emphasis
explicit and normalize colors without adopting that shared-table side effect.

### Named layout map

The ELF retains 2,483 named layout objects and 24 property/depth tables. All 2,481
non-null references resolve uniquely; the other two named objects are browser and
start roots. Actual ARM image-property construction succeeded for 1,261 properties,
verifying initial image IDs, local positions and bounds. Seventy more use image
ID -1 and returned failure after partial initialization; dynamic replacement is
not yet traced. This expands the semantic map across pads, browser/filter, source,
keyboard, timers and utility controls. Local coordinates are not final composed
screen positions. Hierarchy construction, transforms and rendering remain open.
Raw layout data and vendor images remain local, with only findings recorded here.

### Recursive construction and parent linking

Actual recursive construction expands the browser's 53 direct properties into
2,837 objects, including shared control definitions instantiated more than once.
A subsequent ARM LoadResource-prefix probe verified all parent pointers against
an independent depth-stack rule across 24 tables (5,265 instances across separate
scenarios). It stops after the linking pass, before later callbacks/window setup;
this is not full LoadResource success or rendered-screen verification. Invalid
initial-image properties remain allocated because the intermediate constructor
dispatch does not propagate the image constructor failure. The local map now
contains named parent relationships, while final transforms, clipping, visibility
and dynamic image replacement remain open.

### Coordinates, visibility and cue-time evidence

All 5,265 constructed layout instances matched an independent ancestor-offset
calculation when render context was explicitly zeroed. Separate helper tests
show that existing context affects the nominal absolute-position result and
local show getters do not account for hidden ancestors. These are initial linked
bounds, not final visible-screen coverage.

Cue-time comparison tests executed 196 millisecond/150-frame cases in both
directions and ten conversions, with external ceil stubbed. The specialized loop
comparison advances the frame OUT position by one before ceiling conversion.
This is useful for reconciling mixed time representations; it does not justify
adding an offset to BiteDJ's direct millisecond ANLZ importer. The local map now
flags the inaccurate cue-comparison pseudocode alongside the earlier beat-detector
reconstruction issue, and packages the scoped verification ledger with exports.

### RX3 writer-to-parser interoperability

Synthetic bytes captured from actual RX3 ARM writers now pass BiteDJ's compiled
production ANLZ parser for PQTZ beat grids, legacy PCOB/PCPT cue banks and extended
PCO2/PCP2 hot cues. The extended fixture verifies empty/ASCII/Unicode comments
(including a surrogate pair), colors and loop fraction. These checks exercise the
serialization boundary, not Track mutation or full application behavior.

Injected legacy write failures expose inconsistent RX3 error propagation: short
cue-entry writes can be logged while the outer writer returns success. That is a
behavior to avoid in independent implementations, not a feature to copy. No device
filesystem was exercised. The local reports retain stubbing assumptions and source
hashes; vendor binaries and pseudocode remain outside this repository.

### Configured importer check and optional-color fix

The earlier extended parser probe enabled Kaitai ICONV conversion; the actual
application configures NONE and decodes comment bytes in fromUtf16BeString.
A corrected configured-parser/helper check preserves the RX3 fixture's empty,
ASCII and Unicode labels. The earlier UTF-8 result applies only to its test build.

The importer did read optional RGB scalars even when the parser had not populated
them. A length guard now supplies no color for absent/partial tails, preserving
the existing/default cue color through setHotCue. Complete RGB tails retain their
exact value. tools/test_rekordbox_optional_color.py compiles the actual helper and
configured parser; ten empty/Unicode-comment and 0–4-color-byte fixtures pass.
No full Track persistence test or app build was performed in this pass.


Short extended-cue parser follow-up: a 40-byte PCP2 fixture exposed unconditional reads of an absent len_comment in optional-tail predicates. Fixed the Kaitai schema and regenerated C++ with compiler 0.11. Production parser/helper regression now passes 21 cases, including three prefilled-storage patterns, zero-byte/empty/Unicode comments and optional RGB/remainder boundaries. Baseline failed the short-record test. Full importer persistence and malformed declared-length enforcement remain unverified.


Extended-cue boundary audit: actual comment helper passes absent, NUL-only, ASCII and Unicode cases, so no speculative helper fix was made. Added schema validation for the 40-byte fixed minimum and comment length within len_entry - 44; both missing checks reproduced parser acceptance of inconsistent lengths with subsequent bytes available. New tools/test_rekordbox_cue_bounds.py verifies 4 helper cases, 6 malformed lengths with early error positions, and 4 adjacent valid entries. Existing 21 RGB cases and the captured RX3 writer fixture still pass. No full app/Track test; other malformed lengths and generic section limits remain open.
