#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <iostream>
#include <stdexcept>

#include "library/dao/playlistdao.h"
#include "library/engine/engineimportregistry.h"
#include "util/db/sqltransaction.h"
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        auto db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName(":memory:");
        check(db.open(), "open");
        QSqlQuery q(db);
        for (const auto& sql : {"PRAGMA foreign_keys=ON",
                     "CREATE TABLE library(id INTEGER PRIMARY KEY)",
                     "INSERT INTO library VALUES(42),(43)",
                     "CREATE TABLE Playlists(id INTEGER PRIMARY KEY,name TEXT,position INTEGER,hidden INTEGER,locked INTEGER DEFAULT 0,date_created TEXT,date_modified TEXT)",
                     "CREATE TABLE PlaylistTracks(id INTEGER PRIMARY KEY,playlist_id INTEGER REFERENCES Playlists(id),track_id INTEGER REFERENCES library(id),position INTEGER,pl_datetime_added TEXT)",
                     "CREATE TABLE engine_import_entities(library_uuid TEXT,entity_kind TEXT,source_id TEXT,local_id INTEGER,source_baseline BLOB,PRIMARY KEY(library_uuid,entity_kind,source_id))",
                     "CREATE TABLE deferred_failure(id INTEGER REFERENCES library(id) DEFERRABLE INITIALLY DEFERRED)"})
            check(q.exec(sql), "schema");
        PlaylistDAO dao;
        dao.initialize(db);
        int added = 0, entries = 0;
        const TrackId track(QVariant(42));
        QObject::connect(&dao, &PlaylistDAO::added, [&](int id) {++added;QSet<int> set;dao.getPlaylistsTrackIsIn(track,&set);check(set.contains(id),"cache not ready at publication"); });
        QObject::connect(&dao, &PlaylistDAO::trackAdded, [&](int, TrackId, int) { ++entries; });
        mixxx::EngineImportRegistry registry(db);
        const mixxx::EngineImportKey key{"fixture", "playlist", "source-1"};
        std::optional<mixxx::EngineImportRecord> record;
        QString error;
        PlaylistDAO::StagedPlaylist staged;
        {
            SqlTransaction tx(db);
            check(dao.stageNewPlaylist(tx, "Source", {track, TrackId(QVariant(43)), track}, &staged, &error), "stage rollback fixture");
            check(registry.save(key, {staged.id, QJsonObject{}}, &error), "stage provenance rollback");
            check(staged.entryIds.size() == 3 && staged.entryIds[0] != staged.entryIds[2], "duplicate occurrences need distinct IDs");
            check(added == 0 && entries == 0, "premature signal");
            QSet<int> set;
            dao.getPlaylistsTrackIsIn(track, &set);
            check(set.isEmpty(), "premature cache write");
            check(tx.rollback(), "rollback");
            check(!dao.commitStagedPlaylists(tx, {staged}, &error), "published rolled-back transaction");
        }
        check(registry.load(key, &record, &error) && !record, "provenance survived rollback");
        check(q.exec("SELECT COUNT(*) FROM Playlists") && q.next() && q.value(0).toInt() == 0, "rows after rollback");
        q.finish();
        {
            SqlTransaction tx(db);
            check(dao.stageNewPlaylist(tx, "Source", {track, TrackId(QVariant(43)), track}, &staged, &error), "stage commit");
            check(registry.save(key, {staged.id, QJsonObject{}}, &error), "stage provenance commit");
            check(added == 0 && entries == 0, "premature signals on success");
            check(dao.commitStagedPlaylists(tx, {staged}, &error), "commit publication");
            check(registry.load(key, &record, &error) && record && record->localId == staged.id, "committed provenance missing");
            check(added == 1 && entries == 3, "committed notifications");
            check(!dao.commitStagedPlaylists(tx, {staged}, &error) && added == 1, "double publication");
        }
        {
            SqlTransaction tx(db);
            PlaylistDAO::StagedPlaylist invalid;
            check(!dao.stageNewPlaylist(tx, "Missing", {TrackId(QVariant(999))}, &invalid, &error) && invalid.id == -1, "missing track accepted");
            check(dao.stageNewPlaylist(tx, "Next", {track}, &invalid, &error), "savepoint failure poisoned transaction");
            check(tx.rollback(), "rollback after rejected stage");
        }
        {
            SqlTransaction tx(db);
            check(dao.stageNewPlaylist(tx, "Changed", {track}, &staged, &error), "stage changed");
            check(q.exec("UPDATE PlaylistTracks SET position=99 WHERE playlist_id=" + QString::number(staged.id)), "mutate staged rows");
            check(!dao.commitStagedPlaylists(tx, {staged}, &error) && bool(tx) && added == 1, "changed rows committed/published");
            check(tx.rollback(), "rollback changed");
        }
        {
            SqlTransaction tx(db);
            check(dao.stageNewPlaylist(tx, "Commit failure", {track}, &staged, &error), "stage deferred failure");
            check(q.exec("INSERT INTO deferred_failure VALUES(999)"), "deferred constraint fixture");
            check(!dao.commitStagedPlaylists(tx, {staged}, &error) && added == 1 && entries == 3, "failed commit published");
            check(tx.rollback(), "failed commit rollback");
        }
        {
            SqlTransaction tx(db);
            PlaylistDAO::StagedPlaylist kept, rejected;
            check(dao.stageNewPlaylist(tx, "Kept", {track}, &kept, &error), "first staged playlist");
            check(q.exec("CREATE TEMP TRIGGER fail_entry BEFORE INSERT ON PlaylistTracks WHEN NEW.track_id=43 BEGIN SELECT RAISE(ABORT,'fixture'); END"), "entry failure trigger");
            check(!dao.stageNewPlaylist(tx, "Rejected", {track, TrackId(QVariant(43))}, &rejected, &error) && rejected.id == -1, "mid-entry failure accepted");
            check(q.exec("SELECT COUNT(*) FROM Playlists WHERE name='Rejected'") && q.next() && q.value(0).toInt() == 0, "partial playlist survived savepoint rollback");
            q.finish();
            check(q.exec("DROP TRIGGER fail_entry"), "drop fixture trigger");
            check(added == 1 && entries == 3, "mid-entry failure published");
            check(dao.commitStagedPlaylists(tx, {kept}, &error), "prior staged work lost after savepoint failure");
            check(added == 2 && entries == 4, "prior staged publication failed");
        }
        // Update an existing playlist while retaining occurrence identities.
        PlaylistDAO::StagedPlaylist before;
        before.id = dao.getPlaylistIdFromName("Source");
        before.name = "Source";
        check(q.exec("SELECT id,track_id FROM PlaylistTracks WHERE playlist_id=" + QString::number(before.id) + " ORDER BY position"), "snapshot entries");
        while (q.next()) {
            before.entryIds.append(q.value(0).toLongLong());
            before.tracks.append(TrackId(q.value(1)));
        }
        q.finish();
        check(before.entryIds.size() == 3, "snapshot shape");
        int renamed = 0, removed = 0;
        QObject::connect(&dao, &PlaylistDAO::renamed, [&](int, const QString&) { ++renamed; });
        QObject::connect(&dao, &PlaylistDAO::trackRemoved, [&](int, TrackId, int) { ++removed; });
        PlaylistDAO::StagedPlaylist updated;
        const QList<TrackId> desired{track, track, TrackId(QVariant(43))};
        const QList<qint64> desiredIds{before.entryIds[2], before.entryIds[0], 0};
        {
            SqlTransaction tx(db);
            check(dao.stagePlaylistUpdate(tx, before, "Renamed", desired, desiredIds, &updated, &error), "stage existing reorder");
            check(updated.entryIds[0] == before.entryIds[2] && updated.entryIds[1] == before.entryIds[0] && updated.entryIds[2] != before.entryIds[1], "occurrence IDs not preserved");
            check(renamed == 0 && removed == 0 && entries == 4, "update published before commit");
            check(tx.rollback(), "update rollback");
        }
        {
            SqlTransaction tx(db);
            check(q.exec("UPDATE Playlists SET locked=1 WHERE id=" + QString::number(before.id)), "lock fixture");
            check(!dao.stagePlaylistUpdate(tx, before, "Renamed", desired, desiredIds, &updated, &error), "locked playlist edited");
            check(tx.rollback(), "lock rollback");
        }
        {
            SqlTransaction tx(db);
            auto stale = before;
            stale.entryIds.swapItemsAt(0, 2);
            check(!dao.stagePlaylistUpdate(tx, stale, "Renamed", desired, desiredIds, &updated, &error), "stale snapshot accepted");
            auto foreign = desiredIds;
            foreign[0] = 999999;
            check(!dao.stagePlaylistUpdate(tx, before, "Renamed", desired, foreign, &updated, &error), "foreign occurrence accepted");
            check(q.exec("CREATE TEMP TRIGGER fail_update_entry BEFORE INSERT ON PlaylistTracks WHEN NEW.track_id=43 BEGIN SELECT RAISE(ABORT,'fixture'); END"), "update trigger");
            check(!dao.stagePlaylistUpdate(tx, before, "Renamed", desired, desiredIds, &updated, &error), "partial update accepted");
            check(q.exec("DROP TRIGGER fail_update_entry"), "drop update trigger");
            // Exact old snapshot is still accepted after partial writes roll back.
            check(dao.stagePlaylistUpdate(tx, before, "Renamed", desired, desiredIds, &updated, &error), "savepoint did not restore old rows");
            check(dao.commitStagedPlaylists(tx, {updated}, &error), "commit updated playlist");
            check(renamed == 1 && removed == 3 && entries == 7 && added == 2, "update publication counts");
            QList<TrackId> actualOrder;
            check(q.exec("SELECT track_id FROM PlaylistTracks WHERE playlist_id=" + QString::number(before.id) + " ORDER BY position,id"), "ordered readback");
            while(q.next()) actualOrder.append(TrackId(q.value(0)));
            q.finish();
            check(actualOrder == desired, "committed order mismatch");
        }
        {
            SqlTransaction tx(db);
            PlaylistDAO::StagedPlaylist replaced;
            check(dao.stagePlaylistUpdate(tx, updated, updated.name, updated.tracks, {updated.entryIds[0], updated.entryIds[1], 0}, &replaced, &error), "replace highest occurrence");
            check(replaced.entryIds[2] != updated.entryIds[2], "removed occurrence ID reused");
            check(tx.rollback(), "replacement rollback");
        }
        {
            check(q.exec("SELECT COUNT(*) FROM Playlists") && q.next(), "count before outer rollback");
            const int beforeCount = q.value(0).toInt();
            q.finish();
            const int beforeSignals = added;
            check(q.exec("CREATE TEMP TRIGGER rollback_import BEFORE INSERT ON PlaylistTracks WHEN NEW.track_id=43 BEGIN SELECT RAISE(ROLLBACK,'outer transaction aborted'); END"), "outer rollback trigger");
            SqlTransaction tx(db);
            PlaylistDAO::StagedPlaylist rejected;
            check(!dao.stageNewPlaylist(tx, "Aborted", {TrackId(QVariant(43))}, &rejected, &error), "outer rollback stage accepted");
            check(!tx, "wrapper remained active after SQLite rollback");
            check(!dao.stageNewPlaylist(tx, "Unsafe retry", {track}, &rejected, &error), "dead transaction retry accepted");
            check(!tx.commit() && !tx.rollback(), "dead transaction completed twice");
            check(q.exec("SELECT COUNT(*) FROM Playlists") && q.next() && q.value(0).toInt() == beforeCount, "retry persisted outside transaction");
            q.finish();
            check(added == beforeSignals, "aborted transaction published");
            check(q.exec("DROP TRIGGER rollback_import"), "drop outer rollback trigger");
            SqlTransaction fresh(db);
            check(fresh && !tx, "dead wrapper revived with new transaction");
            check(dao.stageNewPlaylist(fresh, "Fresh retry", {track}, &rejected, &error), "fresh transaction retry failed");
            check(fresh.rollback(), "fresh retry rollback");
        }
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0) == "ok", "integrity");
        std::cout << "PASS staged playlists: rollback silence, ordered duplicate occurrence IDs, commit-only notifications/cache, changed-row rejection, missing-track savepoint recovery, failed-commit silence and integrity\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
