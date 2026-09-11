# Engine parent protocol validator

Run `python3 os/tests/test_engine_import_package.py` with Qt6 Core development files and a C++20 compiler. An optional `--fixture /path/to/package.json` additionally checks a locally generated reader package; no private fixture is required for the synthetic tests.

The runner compiles the actual application validator and its non-fast-math floating-point classification boundary under optimized fast-math flags. It verifies protocol/schema identity, distinct cue banks, fractional marker positions and negative grid anchors, exact string source IDs, duplicate playlist membership, unresolved external references, malformed cue data and cyclic/missing/ambiguous hierarchy. The validator is also part of the full application build when Engine export support is enabled.

This function accepts an already parsed QJsonObject. The calling import process still must enforce input size/time limits and reject malformed JSON before calling it. Success is not permission to replace existing tracks/cues, proof of media availability, validation of actual decoded audio bounds, or an import transaction. Source tracking, local edit protection, re-import behavior and UI integration remain separate work.
