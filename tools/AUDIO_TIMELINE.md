# Audio timeline diagnostic for Engine import

Run against a completed Linux BiteDJ build:

```sh
python3 tools/test_audio_timeline.py /path/to/build \
  --ninja /path/to/ninja --output /path/to/timeline-results.json
```

Requires Python, ffmpeg/ffprobe with MP3/AAC/FLAC encoders, the build's compiler
and dependencies, and `compile_commands.json`. The runner links the probe against
the completed application archive. It creates synthetic stereo audio in a temporary
directory and never opens a user profile or music library. It selects providers
through the real `SoundSourceProxy`, opens each source, reads its first 64 frames,
and reports the exposed range. It then decodes the entire exposed range, compares
PCM with FFmpeg CLI output, and seeks back to verify the final block. Temporary
PCM files are removed with the fixtures.
The command succeeds when the diagnostic runs correctly; it deliberately reports
range discrepancies rather than treating them as a passing alignment test.

## Finding on 2026-09-11

Production commit `02c1789`, Linux FFmpeg provider `n9.0.1`, 44.1 kHz stereo,
three-second generated source:

| Format | BiteDJ exposed frames | FFmpeg decoded frames | Difference |
| --- | ---: | ---: | ---: |
| WAV (libsndfile) | 132300 | 132300 | 0 |
| MP3 CBR | 131195 | 132300 | -1105 |
| MP3 VBR | 131195 | 132300 | -1105 |
| AAC/M4A | 132300 | 132300 | 0 |
| FLAC (libFLAC) | 132300 | 132300 | 0 |

Both MP3 containers report start_pts=353600, time_base=1/14112000, and
duration_ts=42336000. The start offset converts to 1105 audio frames; duration
converts to 132300 frames. `SoundSourceFFmpeg::getStreamEndTime` treats duration
as an absolute end timestamp, and the frame conversion subtracts stream start.
For these files, that arithmetic explains the discrepancy exactly. A fix still
needs full PCM/end-of-track and seek verification, including other FFmpeg versions
and MP3s without gapless metadata. This observation alone does not justify changing
all codecs' duration handling or applying a global cue shift.

## Import implications

The current Engine reader exposes raw cue/loop/beat offsets plus sample rate and
sample count. Its `track_snapshot` contract does not establish the native Engine
codec-delay convention. BiteDJ's zero-based decoder range does not establish that
either. Identical rates, filenames, or even lengths cannot independently prove
that cue zero refers to the same PCM sample.

Metadata and playlist identities can be imported without solving this timing
question. Cue/loop/grid application needs matched native Engine and BiteDJ evidence
for the same audio event and file, with codec/provider context. A BiteDJ-generated
Engine export roundtrip tests persistence, not native Engine's decoding convention.
The Pi remains powered off; this run contains no native Engine alignment test.

## Targeted correction and validation

FFmpeg commit [5a526fd](https://github.com/FFmpeg/FFmpeg/commit/5a526fdad01054d611be715099432bc7ac94a14b)
changed MP3 duration to subtract known padding. The correction uses duration as a
length for the MP3 demuxer with MP3 codec on libavformat 62+ (FFmpeg 8+). Other
codecs, demuxers, and older runtime versions retain their previous calculation.
Older runtimes were not executed in this validation.

Run with `--require-complete-mp3` to enforce exact full length for the generated
gapless CBR/VBR MP3 fixtures. Before the correction both are 1105 frames short;
after it both expose 132300 frames, and the entire decoded PCM matches FFmpeg CLI
exactly. Sequential and random end-block reads agree. The existing
`tools/test_ffmpeg_mp3_seek.py` also passes for WAV and mono/stereo CBR/VBR MP3.

The no-Xing fixture remains an estimate: 133630 exposed versus 133632 CLI-decoded
frames in this runtime, unchanged by the correction. WAV/FLAC PCM matches exactly.
AAC exposes the expected length but has a maximum PCM difference of about 0.001881
in this diagnostic (which reads the beginning, seeks back, and decodes again).
AAC PCM equality is reported rather than asserted; AAC handling is unchanged by
the MP3 correction. These results do not prove Engine cue alignment.
