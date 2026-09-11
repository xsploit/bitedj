# Native Engine reader inspection (optional research)

The entry-only probe checks the relocated reader type and read-wrapper pointer in a user-supplied native Engine executable under PC ARM emulation. It deliberately intercepts process startup and exits before Engine main or decoder invocation. Passing is not audio decoding, cue compatibility or Pi performance evidence.

The offsets are bound to executable SHA256 `76fd56d8906a9818d3de3cdad03dc37d0eb1594fa910ec3ec3796f63b673e888`. The runner rejects other hashes. No firmware binary, key or proprietary library is supplied; the caller provides the preserved runtime and an existing Zig compiler. This is separate from the public Engine library reader and is not installed by its CMake build.

```sh
python3 check-decoder-entry.py \
  --runtime /path/to/preserved/runtime64 \
  --zig /path/to/zig \
  --result /path/to/entry-result.json
```

Requires Linux user/network namespaces and `qemu-aarch64-static` (override with `--qemu`). The runner compiles an ARM64 preload shim in a temporary directory, verifies the executable hash, runs with an isolated network namespace and a20-second limit, and records exit status, source/runtime hashes and output. It does not connect to a Pi or launch Engine's UI. No music is read by this entry probe.

The PCM probe below implements the native file-provider and PCM/seek stage. The known reader constructor at0x1619610 consumes ownership of such an object; passing a filename or guessed structure would not test the real interface correctly. See `os/tests/ENGINE_FRAME_ORIGIN.md` for the remaining timing gate.

## Native PCM and repeated seeks

`check-decoder-pcm.py` constructs Engine's actual `airFileFactory`, obtains its native file object with lock state, and passes it to the native reader. It reads sequential float32 PCM and nine seek positions through the real wrapper. Application main remains intercepted. Each test exits without object teardown; this is a bounded research process, not an embeddable player.

```sh
python3 check-decoder-pcm.py \
  --runtime /path/to/preserved/runtime64 \
  --zig /path/to/zig \
  --input /path/to/synthetic-signal.mp3 \
  --pcm-output /path/to/results/native \
  --result /path/to/results/native.json
```

Outputs are `native.stream.f32` and `native.seek0.f32` through `native.seek8.f32`. Stdout records interleaved sample positions/counts. The loop is capped at300 blocks of4096 samples; use short synthetic inputs. The runner requires successful compilation before launching, checks the executable hash, verifies unchanged input bytes, and captures a diagnostic stack on native faults. Passing means execution succeeded, not that cross-decoder alignment is established.

`native-reader-fixture-results.json` records the completed comparison with BiteDJ's actual providers. WAV is exact; CBR/VBR MP3 have equal decoded lengths and maximum PCM differences below0.000001. For those initial stereo fixtures, native WAV/MP3 repeated seeks match sequential output exactly. AAC best aligns with a1024-frame later point in BiteDJ, with a different tail length and small seek differences. No universal AAC correction is established.

Fixtures: stereo signed16 WAV,44100 Hz,132300 frames; left=`int(9000*sin(i*0.062))`, right=`int(6000*sin(i*0.037))`. FFmpeg encodings: `libmp3lame -b:a 192k`, `libmp3lame -q:a 2`, and `aac -b:a 192k` in M4A. BiteDJ was measured using its completed-archive `tools/audio_timeline_probe.cpp` on those exact files. Hashes and provider/build evidence are in the JSON. The expanded checks below cover additional rates, mono, and MP3 without a Xing header. Other encoders and source database cue semantics remain unverified.

## Reproduce the mono VBR seek experiment

`native-reader-expanded-results.json` contains fifteen additional three-second fixtures: mono 44100 Hz, mono 48000 Hz, and stereo 48000 Hz, each as WAV, CBR MP3, VBR MP3, MP3 without a Xing header, and AAC/M4A. WAV sources were resampled/downmixed from the signal above; encodings use the same settings, with `-write_xing 0` for the no-Xing case. The native mono VBR reader returns silence at some nonzero seek positions despite correct sequential decoding.

Use separate output prefixes for each run. First capture the normal sequential reference with the command above. Then open a fresh reader and seek without first decoding sequentially:

```sh
python3 check-decoder-pcm.py \
  --runtime /path/to/preserved/runtime64 \
  --zig /path/to/zig \
  --input /path/to/synthetic-mono-vbr.mp3 \
  --fresh-seek 8190 \
  --pcm-output /path/to/results/fresh-original \
  --result /path/to/results/fresh-original.json
```

Repeat with `--experimental-preroll-nine` and a new output/result prefix to test the process-local change from three codec frames of preroll to nine. Compare each fresh run's `.seek0.f32` against 256 floats beginning at interleaved sample 8190 in the original `.stream.f32`. Repeat at 88200 and use stereo VBR as a control. Fresh-reader mode writes an empty `.stream.f32`; it produces only `.seek0.f32` from the newly opened decoder. Positions are interleaved sample indices, not per-channel frame indices.

The experimental flag modifies one guarded instruction in process memory, restores executable page permissions, and clears the instruction cache. It affects the common reader, not only MP3. It is **not a deployment adapter or a UI-lag fix**. The default command does not patch Engine; the Python runner clears inherited experiment selectors and explicitly records the selected mode. Both input and executable hashes are rechecked after execution.

All tested WAV/MP3 seeks in the expanded matrix match sequential PCM with the experiment. The fresh-reader controls reproduce the original mono failures before any other read and resolve them with the change. AAC still has separate alignment/seek differences. `passed` in a run JSON means the selected harness executed successfully with unchanged files, not that its output matched the reference. Keep the PCM comparison as a separate gate.

## Native cue reset (synthetic object state)

`check-cue-reset.py` invokes the actual native `CueData` constructor with eight slots, verifies its vtable, and exercises its getter, setters and reset routine under PC ARM emulation. It accepts the same `--runtime`, `--zig`, `--qemu` and `--result` arguments as the entry probe. It requires successful compilation, rejects other executable hashes, checks that runtime bytes remain unchanged, isolates networking and limits execution to 20 seconds.

```sh
python3 check-cue-reset.py \
  --runtime /path/to/preserved/runtime64 \
  --zig /path/to/zig \
  --result /path/to/cue-reset-result.json
```

The constructor initializes the main value to -1 and flag to zero. Changing the flag alone leaves the main value unchanged. Reset copies the secondary double to the main double and clears the flag. The fixtures exercise a fractional positive value and zero. `native-cue-reset-results.json` records the successful run.

This establishes object-level behavior only. Neither SQLite deserialization nor source-library loading is invoked, so the secondary field is not claimed to be the database default cue. Zero surviving reset does not establish a database sentinel rule. No decoder, Engine main, Pi or music is used; the bounded process exits without object teardown. This does not enable cue import.
