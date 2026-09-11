# Engine cue frame origin: investigation status

Do not enable cue/loop/grid import based solely on equal sample rates and file sizes. Those checks do not establish that Engine and BiteDJ assign the same frame number to the same audible sample.

Read-only investigation on 2026-09-11 found explicit timestamp translation in the preserved native Engine reader. The inspected executable SHA256 is `76fd56d8906a9818d3de3cdad03dc37d0eb1594fa910ec3ec3796f63b673e888`. No firmware executable or disassembly is distributed here.

## Evidence

Historical native logs for one MP3 load reported a 44100 Hz stream, 1105 skipped MP3 samples, packet-start delay zero, packet timestamp zero and first decoded-frame timestamp353600 at time base1/14112000. Decoder diagnostics explicitly reported skipping the1105 samples. This proves an observed decoder trim, not the database cue coordinate convention.

Static inspection then located:

- Timestamp calibration around0x160f034–0x160f06c, subtracting the recorded MP3 skip count when deriving packet-start delay.
- Common read routine0x1611be0, adding packet-start delay to the requested frame position and translating decoded-frame timestamps with the skip count and another origin field before choosing a copy offset (0x1611d64–0x1611d74 and0x1611f3c–0x1611f88).
- Wrapper0x1612420, dividing an incoming interleaved sample position/count by source channel count before entering that frame-based reader, then converting the returned count back. Its relocated function pointer is at0x2ae1c70.

These are static observations checked against actual instructions. The stripped executable's nearest surviving C++ symbol labels are not reliable function names. They must not be used to infer this reader's identity or signature.

## Consequence for the importer

An additional blanket1105-sample adjustment is not justified: the decoder and native reader already account for MP3 priming. The separate origin field, relevant callers and database cue convention still need confirmation. Timestamp arithmetic in this path uses single precision; no long-track precision result has been established.

The next gate is an actual native-wrapper PCM and seek comparison against BiteDJ using identical synthetic MP3 and AAC files. Establish byte identity, map an audible event to source/local frame positions, and include beginning/end and repeated-seek cases. Direct FFmpeg output alone does not prove native-wrapper behavior. AAC needs independent evidence.

Until that gate passes, the metadata/playlist importer retains timing values in provenance and leaves Engine timing unapplied. Pi and physical-controller testing remain separate requirements.

## PC execution checkpoint

The optional `tools/engine-reader/diagnostics/check-decoder-entry.py` probe passed under ARM emulation with the preserved executable's real shared libraries. It verifies relocated RTTI and the read-wrapper function pointer, then exits before Engine main or any decoder call. This establishes a PC execution entry point; it does not close the audio/seek gate.

The verified constructor is0x1619610, with a0x150-byte reader allocation observed at its caller. It consumes an owning input-object pointer and opens through that object's virtual interface. The constructor initializes origin field0xd0 and skip field0xd8 to zero before opening. A filename string or fabricated file-object layout must not be substituted for the native provider in a claimed full-wrapper test.

## Native PCM comparison completed for four fixtures

The native file factory and reader now execute under PC ARM emulation without application main. Compared against BiteDJ's completed providers on identical three-second stereo44100-Hz synthetic files:

| Fixture | Native frames | BiteDJ frames | Maximum same-offset PCM difference |
|---|---:|---:|---:|
| WAV |132300|132300|0|
| MP3 CBR |132300|132300|0.000000507|
| MP3 VBR |132300|132300|0.000000536|
| AAC/M4A |132096|132300|0.425|

For those four fixtures, native WAV/MP3 seeks, including the last frame and beyond EOF, match sequential PCM exactly. For the AAC comparison's first8000 frames, shifting the BiteDJ reference forward1024 frames reduces RMS difference from about0.0933 to0.00000870. Native AAC reports origin=-1024; its tail count also differs. This argues against assuming unadjusted AAC alignment, not for a universal offset rule.

See `tools/engine-reader/diagnostics/` for the harness, fixture details and hashed results. Passing runs use the actual native allocation/file-factory and lock-state setup, without assertion bypasses or fake file providers. Broader codec/rate/channel coverage and source database cue-coordinate confirmation remain before enabling timing import.

## Expanded rate/channel checks and seek-preroll experiment

Fifteen additional synthetic three-second fixtures cover mono 44100 Hz, mono 48000 Hz and stereo 48000 Hz, each as WAV, CBR MP3, VBR MP3, MP3 without a Xing header, and AAC/M4A. See `tools/engine-reader/diagnostics/native-reader-expanded-results.json` for input hashes and individual comparisons.

Sequential WAV and MP3 PCM remains closely aligned with BiteDJ. The mono 44100-Hz MP3 without a Xing header has 133632 native frames versus 133630 advertised by BiteDJ, so exact duration is not established for that case. AAC retains the observed 1024-frame initial alignment difference and unequal frame counts; this remains fixture evidence, not a universal correction.

Native mono VBR MP3 seeks expose a separate correctness issue: at both tested rates, reads at interleaved sample positions 88200 and 8190 return 256 zero samples although sequential PCM there is nonzero. BiteDJ's existing tail-seek check passes for these files; that check does not cover every native probe position.

An isolated process-local experiment increases the native common reader's preroll from three codec frames to nine. The instruction at image offset 0x1611c50 is guarded as 0x12800043 and replaced in memory with 0x12800103. The preserved executable SHA256 remains unchanged. This is not deployed or included in the default probe.

With that change, every tested WAV/MP3 seek in the fifteen-fixture matrix matches its sequential PCM exactly, including both previously failing mono VBR files. WAV/MP3 sequential output is byte-identical before and after. This establishes that increasing preroll removes these observed failures; bit-reservoir recovery is a plausible mechanism, not separately proven. BiteDJ already has an explicit MP3 preroll policy; its channel-dependent sample units should not be conflated with this native codec-frame multiplier.

AAC sequential frame counts are unchanged by the experiment, with numerical differences at most 2.98e-8. AAC seek differences remain. The change currently affects the common decoder path and needs MP3-specific scoping, broader seek-order and file coverage, and live playback cost validation before becoming an adapter candidate. It does not address Engine's observed UI loading lag or establish source database cue coordinates.

A follow-up control opens a fresh native reader for each position (8190 and 88200) without first reading sequentially or beyond EOF. Both mono VBR files still return silence with the original instruction, and exact sequential-reference PCM with the nine-frame experiment. The stereo VBR control passes both ways. This rules out the original probe's preceding out-of-range seek as a necessary cause of these failures. The surrounding native instructions multiply the immediate by the decoder frame-size field before adding the requested position; nearest surviving symbol names are unrelated stripped-symbol artifacts.

## AAC noise-substitution control

The BiteDJ provider probe now reports tail-seek maximum absolute and RMS PCM differences in addition to exact byte equality. For the stereo 48000-Hz AAC fixture, the last 64 frames have the correct length but differ after seeking (maximum 0.00243612, RMS 0.000894006). The mono AAC and stereo VBR MP3 controls match exactly.

Matched encodes of the same synthetic WAV using `-c:a aac -b:a 192k` with `-aac_pns 0` and `-aac_pns 1` isolate the encoder's perceptual-noise-substitution option. With PNS disabled, BiteDJ's tail comparison is exact; enabling it reproduces the original difference. FFmpeg's [documented noise-band decoder](https://www.ffmpeg.org/doxygen/8.0/aacdec__proc__template_8c_source.html) advances a random state while synthesizing noise. That source illustrates a plausible state-dependent mechanism; it is not verification of every instruction in the tested FFmpeg n9.0.1 binary. The controlled encode result does not establish audibility or a quality regression, and byte inequality alone must not be treated as a seek-position failure for all AAC.

The Engine/BiteDJ frame-origin difference persists without PNS. For the no-PNS stereo 48000-Hz fixture, comparing the first 8000 native frames to BiteDJ at offset zero gives RMS 0.272969; shifting the BiteDJ reference forward 1024 frames gives RMS 1.44e-8 and maximum difference 1.04e-7. Native returns 143360 frames versus BiteDJ's 144000 and reports origin=-1024. This separates the observed frame-origin issue from noise-substitution differences. See `tools/engine-reader/diagnostics/aac-pns-findings.json` for hashes and measurements. No universal AAC offset or database-cue translation is enabled.
