#include <QCoreApplication>
#include <QFile>
#include <QSqlQuery>
#include <iostream>
#include <stdexcept>

#include "library/engine/engineimportmerge.h"
#include "library/engine/engineimportregistry.h"

using namespace mixxx;
static void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QJsonObject a{{"label", "A"}}, b{{"label", "B"}}, c{{"label", "C"}};
        auto initial = mergeEngineImportFields({}, {}, a);
        check(initial.values == a && initial.baseline == a && initial.changedFields.size() == 1, "Initial import failed");
        auto repeated = mergeEngineImportFields(a, a, a);
        check(repeated.changedFields.isEmpty() && repeated.conflicts.isEmpty(), "Reimport not idempotent");
        auto localEdit = mergeEngineImportFields(a, b, a);
        check(localEdit.values == b && localEdit.baseline == a && localEdit.conflicts.isEmpty(), "Local edit overwritten");
        auto sourceEdit = mergeEngineImportFields(a, a, c);
        check(sourceEdit.values == c && sourceEdit.baseline == c, "Source update not applied");
        auto conflict = mergeEngineImportFields(a, b, c);
        check(conflict.values == b && conflict.baseline == a && conflict.conflicts == QStringList{"label"}, "Conflict lost");
        auto retry = mergeEngineImportFields(conflict.baseline, conflict.values, c);
        check(retry.conflicts == conflict.conflicts && retry.values == b, "Conflict vanished on repeat");
        auto resolved = mergeEngineImportFields(a, c, c);
        check(resolved.baseline == c && resolved.conflicts.isEmpty(), "Resolved conflict not accepted");
        auto omitted = mergeEngineImportFields(a, b, {});
        check(omitted.values == b && omitted.baseline == a, "Missing source field deleted local data");
        QJsonObject cleared{{"label", QJsonValue::Null}};
        check(mergeEngineImportFields(a, a, cleared).values == cleared, "Explicit null not applied");
        check(!mergeEngineImportFields({}, b, c).conflicts.isEmpty(), "Unowned local field overwritten");
        check(mergeEngineImportFields({}, b, b).baseline == b, "Equal unowned field not recorded");
        auto combined = mergeEngineImportFields({{"label", "A"}, {"color", 1}}, {{"label", "B"}, {"color", 1}}, {{"label", "C"}, {"color", 2}});
        check(combined.conflicts == QStringList{"label"} && combined.values["color"] == 2 && combined.baseline["label"] == "A", "Independent fields not merged safely");

        auto db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(":memory:");
        check(db.open(), "Database open");
        QSqlQuery q(db);
        check(q.exec("CREATE TABLE local_entities(id INTEGER PRIMARY KEY, title TEXT)"), "Local fixture schema");
        check(argc == 2, "Migration SQL missing");
        QFile sql(argv[1]);
        check(sql.open(QIODevice::ReadOnly), "Migration read");
        const auto migration = QString::fromUtf8(sql.readAll());
        check(q.exec(migration), "Production schema41 failed");
        EngineImportRegistry registry(db);
        QString error;
        std::optional<EngineImportRecord> record;
        EngineImportKey key{"fixture-library", "cue", "loop:1"};
        check(registry.load(key, &record, &error) && !record && error.isEmpty(), "Missing identity not distinguished");
        check(db.transaction(), "Begin transaction");
        check(q.exec("INSERT INTO local_entities VALUES(1,'Imported')"), "Insert local");
        check(registry.save(key, {1, a}, &error), "Save source state");
        check(db.rollback(), "Rollback");
        check(registry.load(key, &record, &error) && !record, "Provenance escaped rollback");
        check(q.exec("SELECT count(*) FROM local_entities") && q.next() && q.value(0).toInt() == 0, "Local change escaped rollback");
        check(db.transaction() && registry.save(key, {1, conflict.baseline}, &error) && db.commit(), "Commit source state");
        check(registry.load(key, &record, &error) && record && record->localId == 1 && record->baseline == a, "Persistent baseline lost");
        check(q.exec(migration), "Reapplying schema41 failed");
        check(registry.load(key, &record, &error) && record && record->baseline == a, "Reapplying migration lost data");
        check(registry.save(key, {1, c}, &error), "Update registry");
        check(q.exec("SELECT count(*) FROM engine_import_entities") && q.next() && q.value(0).toInt() == 1, "Duplicate on reimport");
        EngineImportKey otherLibrary{"other-library", "cue", "loop:1"};
        EngineImportKey otherKind{"fixture-library", "track", "loop:1"};
        EngineImportKey otherBank{"fixture-library", "cue", "hotcue:1"};
        for (const auto& k : {otherLibrary, otherKind, otherBank})
            check(registry.save(k, {std::nullopt, b}, &error), "Identity namespaces collide");
        check(q.exec("SELECT count(*) FROM engine_import_entities") && q.next() && q.value(0).toInt() == 4, "Lost identity namespace");
        check(registry.load(otherBank, &record, &error) && record && !record->localId && record->baseline == b, "Unresolved identity lost");
        check(!registry.save(key, {0, a}, &error) && !error.isEmpty(), "Invalid local ID accepted");
        check(q.exec("UPDATE engine_import_entities SET source_baseline='not-json' WHERE library_uuid='fixture-library' AND entity_kind='cue' AND source_id='loop:1'"), "Corrupt fixture");
        check(!registry.load(key, &record, &error) && !record && !error.isEmpty(), "Corrupt baseline treated as new import");
        check(q.exec("DROP TABLE engine_import_entities"), "Drop fixture");
        check(!registry.load(key, &record, &error) && !record && !error.isEmpty(), "SQL failure treated as new import");
        std::cout << "PASS: three-way merge, persistent conflicts, identity namespaces, idempotent registry, transaction rollback, corrupt/failed read separation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
