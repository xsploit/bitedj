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
