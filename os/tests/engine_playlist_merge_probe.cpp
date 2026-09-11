#include <QCoreApplication>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <iostream>
#include <stdexcept>

#include "library/engine/engineplaylistimport.h"
#include "library/engine/engineimportregistry.h"
#include "util/db/sqltransaction.h"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static TrackId track(int id) { return TrackId(QVariant(id)); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        auto db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(":memory:");
        check(db.open(), "open");
        QSqlQuery q(db);
        for (const auto* sql : {
                "PRAGMA foreign_keys=ON",
                "CREATE TABLE library(id INTEGER PRIMARY KEY)",
                "INSERT INTO library VALUES(42),(43),(44)",
                "CREATE TABLE Playlists(id INTEGER PRIMARY KEY,name TEXT,position INTEGER,hidden INTEGER,locked INTEGER DEFAULT 0,date_created TEXT,date_modified TEXT)",
                "CREATE TABLE PlaylistTracks(id INTEGER PRIMARY KEY,playlist_id INTEGER REFERENCES Playlists(id),track_id INTEGER REFERENCES library(id),position INTEGER,pl_datetime_added TEXT)",
                "CREATE TABLE engine_import_entities(library_uuid TEXT,entity_kind TEXT,source_id TEXT,local_id INTEGER,source_baseline BLOB,PRIMARY KEY(library_uuid,entity_kind,source_id))"}) {
            check(q.exec(sql), "schema");
        }
        PlaylistDAO dao;
        dao.initialize(db);
        mixxx::EngineImportRegistry registry(db);
        const mixxx::EngineImportKey key{"fixture", "playlist", "set-1"};
        QString error;
        PlaylistDAO::StagedPlaylist current;
        QVector<mixxx::EnginePlaylistImportEntry> source{
                {"a", track(42)}, {"b", track(43)}, {"c", track(42)}};
        auto plan = mixxx::planEnginePlaylistImport(current, {}, {}, "Set", source, true);
        check(plan.error.isEmpty() && plan.conflicts.isEmpty() &&
                plan.tracks == QList<TrackId>{track(42), track(43), track(42)} &&
                plan.entryIds == QList<qint64>{0, 0, 0}, "first plan");
        {
            SqlTransaction tx(db);
            check(dao.stageNewPlaylist(tx, plan.name, plan.tracks, &current, &error), "first stage");
            check(registry.save(key, {current.id, plan.baseline}, &error), "first provenance");
            for (qsizetype i = 0; i < source.size(); ++i) {
                check(registry.save({"fixture", "entry", source[i].sourceEntryId},
                        {current.entryIds[i], {}}, &error), "first occurrence provenance");
            }
            check(dao.commitStagedPlaylists(tx, {current}, &error), "first commit");
        }
        QHash<QString, qint64> bindings{{"a", current.entryIds[0]},
                {"b", current.entryIds[1]}, {"c", current.entryIds[2]}};
        check(bindings["a"] != bindings["c"], "duplicates conflated");
        std::optional<mixxx::EngineImportRecord> record;
        check(registry.load(key, &record, &error) && record, "read accepted baseline");
        auto baseline = record->baseline;
        plan = mixxx::planEnginePlaylistImport(current, baseline, bindings, "Set", source);
        check(plan.error.isEmpty() && plan.conflicts.isEmpty() &&
                plan.entryIds == current.entryIds && plan.baseline == baseline, "idempotent import");
        source = {{"c", track(42)}, {"a", track(42)}, {"d", track(43)}};
        plan = mixxx::planEnginePlaylistImport(current, baseline, bindings, "Renamed", source);
        check(plan.conflicts.isEmpty() && plan.entryIds == QList<qint64>{bindings["c"], bindings["a"], 0},
                "source reorder/delete/add identities");
        PlaylistDAO::StagedPlaylist staged;
        {
            SqlTransaction tx(db);
            check(dao.stagePlaylistUpdate(tx, current, plan.name, plan.tracks, plan.entryIds, &staged, &error), "update stage");
            check(registry.save(key, {staged.id, plan.baseline}, &error), "update provenance");
            check(registry.save({"fixture", "entry", "d"}, {staged.entryIds[2], {}}, &error), "staged new occurrence binding");
            check(tx.rollback(), "rollback update");
        }
        check(registry.load(key, &record, &error) && record && record->baseline == baseline,
                "rolled-back baseline advanced");
        check(registry.load({"fixture", "entry", "d"}, &record, &error) && !record,
                "rolled-back occurrence binding survived");
        // Retry against the same exact snapshot also proves the rows rolled back.
        {
            SqlTransaction tx(db);
            check(dao.stagePlaylistUpdate(tx, current, plan.name, plan.tracks, plan.entryIds, &staged, &error), "retry update");
            check(registry.save(key, {staged.id, plan.baseline}, &error), "retry provenance");
            check(registry.save({"fixture", "entry", "d"}, {staged.entryIds[2], {}}, &error), "retry occurrence binding");
            check(dao.commitStagedPlaylists(tx, {staged}, &error), "retry commit");
        }
        current = staged;
        baseline = plan.baseline;
        check(registry.load({"fixture", "entry", "d"}, &record, &error) && record &&
                record->localId == current.entryIds[2], "committed occurrence binding missing");
        bindings.insert("d", *record->localId);
        check(bindings["d"] != bindings["b"], "deleted occurrence ID recycled");
        check(q.exec("SELECT track_id,id FROM PlaylistTracks ORDER BY position,id"), "ordered read");
        int index = 0;
        while (q.next()) {
            check(index < current.tracks.size() && TrackId(q.value(0)) == current.tracks[index] &&
                    q.value(1).toLongLong() == current.entryIds[index], "stored order or identity");
            ++index;
        }
        check(index == 3, "stored occurrence count");
        q.finish();
        auto local = current;
        local.name = "My set";
        local.tracks.append(track(44));
        local.entryIds.append(999);
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, "Renamed", source);
        check(plan.conflicts.isEmpty() && plan.name == "My set" && plan.tracks == local.tracks &&
                plan.entryIds == local.entryIds && plan.baseline == baseline, "local edits overwritten");
        const QVector<mixxx::EnginePlaylistImportEntry> changed{{"d", track(43)}, {"a", track(42)}};
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, "Source edit", changed);
        check(plan.conflicts.contains("name") && plan.conflicts.contains("entries") &&
                plan.name == local.name && plan.entryIds == local.entryIds && plan.baseline == baseline,
                "concurrent edits not preserved");
        const auto repeated = mixxx::planEnginePlaylistImport(local, plan.baseline, bindings, "Source edit", changed);
        check(repeated.conflicts == plan.conflicts && repeated.baseline == baseline, "conflict disappeared on retry");
        local.name = current.name;
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, "Source edit", changed);
        check(plan.name == "Source edit" && plan.conflicts == QStringList{"entries"} &&
                plan.baseline["entries"] == baseline["entries"], "independent rename lost");
        plan = mixxx::planEnginePlaylistImport(current, baseline, bindings, current.name, {});
        check(plan.conflicts.isEmpty() && plan.tracks.isEmpty() && plan.entryIds.isEmpty(), "source clear rejected");
        local = current;
        local.tracks.removeLast(); local.entryIds.removeLast();
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, current.name, source);
        check(plan.conflicts.isEmpty() && plan.entryIds == local.entryIds, "local deletion restored unexpectedly");
        local = current;
        std::swap(local.tracks[0], local.tracks[2]);
        std::swap(local.entryIds[0], local.entryIds[2]);
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, current.name, source);
        check(plan.conflicts.isEmpty() && plan.entryIds == local.entryIds, "local reorder lost");
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, current.name, changed);
        check(plan.conflicts == QStringList{"entries"} && plan.entryIds == local.entryIds, "concurrent reorder merged silently");
        PlaylistDAO::StagedPlaylist namedNew;
        namedNew.name = "Local name";
        check(!mixxx::planEnginePlaylistImport(namedNew, {}, {}, "Source", {}, true).error.isEmpty(), "named local state treated as new");
        namedNew.name.clear(); namedNew.id = current.id;
        check(!mixxx::planEnginePlaylistImport(namedNew, {}, {}, "Source", {}, true).error.isEmpty(), "existing playlist treated as new");
        auto badBaseline = baseline; badBaseline.insert("entries", "broken");
        check(!mixxx::planEnginePlaylistImport(current, badBaseline, bindings, current.name, source).error.isEmpty(), "malformed baseline accepted");
        local = current;
        local.entryIds[0] = 999; // Same song/order, but a new unowned occurrence.
        plan = mixxx::planEnginePlaylistImport(local, baseline, bindings, current.name, source);
        check(plan.error.isEmpty() && plan.conflicts.isEmpty() &&
                plan.entryIds == local.entryIds && !plan.acceptsSourceEntries,
                "unowned replacement occurrence acquired source identity");
        auto invalid = bindings; invalid.insert("alias", bindings["a"]);
        check(!mixxx::planEnginePlaylistImport(current, baseline, invalid, current.name, source).error.isEmpty(), "duplicate binding accepted");
        auto duplicate = source; duplicate.append(source[0]);
        check(!mixxx::planEnginePlaylistImport(current, baseline, bindings, current.name, duplicate).error.isEmpty(), "duplicate source occurrence accepted");
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0).toString() == "ok", "integrity");
        std::cout << "PASS playlist merge + actual DAO/registry: duplicates, reimport, source reorder/delete/add, rollback/retry, local edits/deletion, persistent conflicts, independent fields.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
