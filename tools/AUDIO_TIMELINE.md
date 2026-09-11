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
and reports the exposed range. It separately counts FFmpeg CLI decoded frames.
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
