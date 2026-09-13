#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <iostream>
#include <stdexcept>

#include "database/schemamanager.h"
#include "library/engine/engineimportregistry.h"
#include "library/engine/engineplaylistimport.h"
#include "util/db/sqltransaction.h"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    try {
        auto db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(":memory:");
        check(db.open(), "open");
        QSqlQuery q(db);
        for (const auto* sql : {
                "CREATE TABLE settings(name TEXT UNIQUE,value TEXT,locked INTEGER,hidden INTEGER)",
                "INSERT INTO settings VALUES('mixxx.schema.version','41',0,0),('mixxx.schema.last_used_version','41',0,0),('mixxx.schema.min_compatible_version','3',0,0)",
                "CREATE TABLE Playlists(id INTEGER PRIMARY KEY,name TEXT)",
                "CREATE TABLE PlaylistTracks(id INTEGER PRIMARY KEY,playlist_id INTEGER,track_id INTEGER,position INTEGER)",
                "INSERT INTO Playlists VALUES(1,'Set')",
                "INSERT INTO PlaylistTracks VALUES(1,1,42,1)",
                "CREATE TABLE engine_import_entities(library_uuid TEXT,entity_kind TEXT,source_id TEXT,local_id INTEGER,source_baseline BLOB,PRIMARY KEY(library_uuid,entity_kind,source_id))"}) {
            check(q.exec(sql), "schema41 fixture");
        }
        mixxx::EngineImportRegistry registry(db);
        const mixxx::EngineImportKey entryKey{"fixture", "entry", "a"};
        const mixxx::EngineImportKey playlistKey{"fixture", "playlist", "set"};
        const QJsonObject originalBaseline{{"retained", true}};
        QString error;
        check(registry.save(entryKey, {1, originalBaseline}, &error) &&
                registry.save(playlistKey, {1, originalBaseline}, &error), "initial bindings");
        SchemaManager manager(db);
        check(manager.upgradeToSchemaVersion(42, QString::fromLocal8Bit(argv[1])) ==
                SchemaManager::Result::UpgradeSucceeded, "actual migration42");
        check(manager.readCurrentVersion() == 42, "version42");
        std::optional<mixxx::EngineImportRecord> record;
        for (const auto& key : {entryKey, playlistKey}) {
            check(registry.load(key, &record, &error) && record && !record->localId &&
                    record->baseline == originalBaseline, "uncertain old link retained or baseline lost");
            check(registry.save(key, {1, originalBaseline}, &error), "fresh verified binding");
        }
        {
            SqlTransaction tx(db);
            check(q.exec("DELETE FROM PlaylistTracks WHERE id=1"), "delete within rollback");
            check(registry.load(entryKey, &record, &error) && record && !record->localId, "trigger not immediate");
            check(tx.rollback(), "rollback deletion");
        }
        check(registry.load(entryKey, &record, &error) && record && record->localId == 1,
                "rollback did not restore identity");
        check(q.exec("DELETE FROM PlaylistTracks WHERE id=1"), "delete occurrence");
        check(q.exec("INSERT INTO PlaylistTracks(playlist_id,track_id,position) VALUES(1,44,1)") &&
                q.lastInsertId().toLongLong() == 1, "fixture did not reuse rowid");
        check(registry.load(entryKey, &record, &error) && record && !record->localId &&
                record->baseline == originalBaseline, "recycled row acquired stale identity");
        PlaylistDAO::StagedPlaylist empty;
        const QVector<mixxx::EnginePlaylistImportEntry> original{{"a", TrackId(QVariant(42))}};
        const auto initial = mixxx::planEnginePlaylistImport(empty, {}, {}, "Set", original, true);
        PlaylistDAO::StagedPlaylist local;
        local.id = 1; local.name = "Set";
        local.tracks = {TrackId(QVariant(44))}; local.entryIds = {1};
        const auto reimport = mixxx::planEnginePlaylistImport(local, initial.baseline, {}, "Set", {});
        check(reimport.error.isEmpty() && reimport.conflicts == QStringList{"entries"} &&
                reimport.tracks == local.tracks, "source deletion removed replacement local song");
        check(q.exec("DELETE FROM PlaylistTracks"), "clear child rows");
        check(q.exec("DELETE FROM Playlists WHERE id=1"), "delete playlist");
        check(q.exec("INSERT INTO Playlists(name) VALUES('Local replacement')") &&
                q.lastInsertId().toLongLong() == 1, "playlist ID not recycled");
        check(registry.load(playlistKey, &record, &error) && record && !record->localId,
                "recycled playlist acquired stale identity");
        check(registry.save(playlistKey, {1, originalBaseline}, &error), "new verified playlist binding");
        check(q.exec("UPDATE settings SET value='41' WHERE name='mixxx.schema.last_used_version'"), "downgrade marker");
        check(manager.upgradeToSchemaVersion(42, QString::fromLocal8Bit(argv[1])) ==
                SchemaManager::Result::UpgradeSucceeded, "reapply42");
        check(registry.load(playlistKey, &record, &error) && record && record->localId == 1,
                "reapply invalidated verified binding");
        check(manager.upgradeToSchemaVersion(42, QString::fromLocal8Bit(argv[1])) ==
                SchemaManager::Result::CurrentVersion, "idempotent42");
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0).toString() == "ok", "integrity");
        std::cout << "PASS actual schema migration42: compound triggers, one-time invalidation, row-ID reuse, rollback, conflict preservation, replay and integrity.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
