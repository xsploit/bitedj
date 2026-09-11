# Engine import baseline and merge foundation

Run `python os/tests/test_engine_import_state.py` with Qt6 Core/SQL development packages and a C++20 compiler. It compiles the actual production merge and registry code with warnings treated as errors, and applies revision 41 from `res/schema.xml` to synthetic in-memory SQLite data.

The field merge compares the last accepted source value, the current local value, and the incoming value. Unchanged local fields accept source changes; local edits survive unchanged source values. Concurrent different edits produce conflicts and retain the old baseline, so repeated imports continue reporting unresolved conflicts. Missing incoming fields are unmanaged; explicit JSON null is a value. Nested objects and arrays are compared as whole field values.

Revision 41 adds an import registry keyed by source library UUID, entity kind and source identity. The source identity must include any parent scope needed for uniqueness: a cue requires its track identity, bank and slot, not just its slot. Capture hashes must not be used as library identity: SQLite backup bytes can vary across SQLite versions. The registry uses the caller's transaction and distinguishes missing records from database/JSON failures.

Coverage includes initial/repeated import, source and local edits, repeated conflicts and resolution, missing/null fields, independent fields, identity namespaces, rollback with a local row, commit, registry replacement without duplicate rows, repeat migration preserving stored state, invalid local IDs, corrupt baseline JSON and SQL failure.

These are integration components. They do not yet apply a complete package to Track/playlist objects, reconcile caches, allocate cue controls, resolve deleted entities or expose an import action. Whole-import atomicity and end-to-end idempotence remain unverified.
