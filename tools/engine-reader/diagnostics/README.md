# Native Engine reader inspection (optional research)

This probe checks the relocated reader type and read-wrapper pointer in a user-supplied native Engine executable under PC ARM emulation. It deliberately intercepts process startup and exits before Engine main or decoder invocation. Passing is not audio decoding, cue compatibility or Pi performance evidence.

The offsets are bound to executable SHA256 `76fd56d8906a9818d3de3cdad03dc37d0eb1594fa910ec3ec3796f63b673e888`. The runner rejects other hashes. No firmware binary, key or proprietary library is supplied; the caller provides the preserved runtime and an existing Zig compiler. This is separate from the public Engine library reader and is not installed by its CMake build.

```sh
python3 check-decoder-entry.py \
  --runtime /path/to/preserved/runtime64 \
  --zig /path/to/zig \
  --result /path/to/entry-result.json
```

Requires Linux user/network namespaces and `qemu-aarch64-static` (override with `--qemu`). The runner compiles an ARM64 preload shim in a temporary directory, verifies the executable hash, runs with an isolated network namespace and a20-second limit, and records exit status, source/runtime hashes and output. It does not connect to a Pi or launch Engine's UI. No music is read by this entry probe.

The next stage requires a valid native file-provider object and a PCM/seek harness. The known reader constructor at0x1619610 consumes ownership of such an object; passing a filename or guessed structure would not test the real interface correctly. See `os/tests/ENGINE_FRAME_ORIGIN.md` for the remaining timing gate.
