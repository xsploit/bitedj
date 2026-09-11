# Portable Engine cue overrides

The previous per-filesystem cue store only serialized controls0–25 and main cues. It did not serialize Engine source identities. A production-archive regression reproduced an omitted saved loop at local control26; recreating a hot cue on a fresh Track also lost its source identity.

The store now writes version2 rows with a `{ "version": 2, "cues": [...] }` payload. Each cue explicitly carries a valid Engine library/track/bank/slot identity or null. Positions remain in seconds. Version1 array rows remain readable; unchanged loads do not rewrite them. The first actual edit writes version2. Older BiteDJ builds that only read version1 will ignore a version2 row.

Version2 extends management to Engine-identified controls across the current valid control range. It does not globally reinterpret higher custom controls. An unowned custom control occupied by an incoming cue rejects the entire payload before changes. Existing cue QObjects are updated in place. Explicit null clears stale source identity on a plain replacement; legacy entries without identity preserve an existing cue's origin. Legacy empty arrays leave newer Engine controls outside0–25 alone; a new empty snapshot can remove those Engine-identified cues. Intro/outro and analysis cues stay outside the store's ownership.

Version2 validates shape, supported version, source identities, duplicate slots/origins, positions, colors and control ownership before changing cues. Invalid payloads are ignored with a diagnostic. This is a codec/portable-store fix, not a complete Engine import transaction or a live-track publication API. Source override precedence still needs coordination when the full importer applies new source data.

The portable probe links the actual completed application archive:

```
python os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_cue_override_probe.cpp
```

The same probe can exercise the actual filesystem SQLite path in a private user/mount namespace. All `/mnt` mounts below exist only inside that namespace; no real removable drive is needed. Replace paths with absolute paths before running:

```
unshare -Umr --propagation private bash -c 'mount -t tmpfs tmpfs /mnt && mkdir -p /mnt/usbtest && mount -t tmpfs tmpfs /mnt/usbtest && python /absolute/repo/os/tests/test_engine_cue_origin.py BUILD --ninja NINJA --probe-source /absolute/repo/os/tests/engine_cue_override_probe.cpp --probe-arg=--store'
```

Coverage includes separate hot1/loop1 origins, a text ID above2^53, fractional frames, unset hot-cue ends, stable serialization, in-place edits, ordinary pad/memory/main cues, preserved intro cue, legacy compatibility, version2 empty snapshot, occupied custom controls, malformed/future payload rejection, version1 SQLite read/no-op save, version2 write/fresh-track reload, imported-cue restoration and database integrity. It does not simulate a physical USB eject or concurrent deck playback.

An initial probe setup used a conditional selecting between a finite end and NaN under `-ffast-math`; that setup produced a finite end for the hot cue. The corrected fixture constructs an unset end directly and sets only loop ends afterward. It explicitly asserts both the source and reloaded hot-cue ends are unset. This test-setup observation is not a claim that ordinary device playback reproduces the same failure.
