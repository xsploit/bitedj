#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "library/dao/cuedao.h"
#include "util/db/sqltransaction.h"

static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static QList<QVariantList> rows(QSqlDatabase db) {
    QSqlQuery q(db);
    check(q.exec("SELECT id,track_id,type,position,length,hotcue,label,color,engine_library_uuid,engine_track_id,engine_bank,engine_slot FROM cues ORDER BY id"), "Read snapshot");
    QList<QVariantList> result;
    while (q.next()) {
        QVariantList row;
        for (int i = 0; i < 12; ++i)
            row.append(q.value(i));
        result.append(row);
    }
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "Temporary directory");
        auto db = QSqlDatabase::addDatabase("QSQLITE", "staged-cues");
        db.setDatabaseName(temp.filePath("library.sqlite"));
        check(db.open(), "Database open");
        QSqlQuery q(db);
        check(q.exec("PRAGMA foreign_keys=ON"), "Foreign keys");
        check(q.exec("CREATE TABLE cues(id INTEGER PRIMARY KEY AUTOINCREMENT,track_id INTEGER,type INTEGER,position REAL,length REAL,hotcue INTEGER,label TEXT NOT NULL,color INTEGER,engine_library_uuid TEXT,engine_track_id TEXT,engine_bank INTEGER,engine_slot INTEGER)"), "Cue schema");
        check(q.exec("CREATE TABLE import_state(id INTEGER PRIMARY KEY,baseline TEXT)"), "Baseline schema");
        check(q.exec("INSERT INTO import_state VALUES(1,'old')"), "Baseline seed");
        check(q.exec("CREATE TABLE parent(id INTEGER PRIMARY KEY)"), "Commit parent");
        check(q.exec("CREATE TABLE deferred_child(id INTEGER REFERENCES parent(id) DEFERRABLE INITIALLY DEFERRED)"), "Commit child");
        CueDAO dao;
        dao.initialize(db);
        const TrackId track(QVariant(1));
        using F = mixxx::audio::FramePos;
        auto hot = CuePointer(new Cue(mixxx::CueType::HotCue, 0, F(100.25), F(), mixxx::RgbColor(0xff8000)));
        auto ordinary = CuePointer(new Cue(mixxx::CueType::HotCue, 5, F(500), F(), mixxx::RgbColor(0x123456)));
        hot->setEngineOrigin(Cue::EngineOrigin{"source-library", "1", Cue::EngineOrigin::Bank::HotCue, 1});
        dao.saveTrackCues(track, {hot, ordinary});
        const auto originalRows = rows(db);
        const auto originalId = hot->getId();
        hot->setLabel("Edited hotcue");
        auto loop = CuePointer(new Cue(mixxx::CueType::Loop, 26, F(1000.25), F(2000.75), mixxx::RgbColor(0xabcdef)));
        loop->setLabel("reject");
        loop->setEngineOrigin(Cue::EngineOrigin{"source-library", "1", Cue::EngineOrigin::Bank::SavedLoop, 1});
        QList<CuePointer> staged;
        QString error;
        check(q.exec("CREATE TRIGGER reject_insert BEFORE INSERT ON cues WHEN NEW.label='reject' BEGIN SELECT RAISE(ABORT,'injected insert failure'); END"), "Insert failure trigger");
        {
            SqlTransaction transaction(db);
            check(bool(transaction), "Begin failure transaction");
            check(q.exec("UPDATE import_state SET baseline='new'"), "Baseline tentative update");
            check(!dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error) && staged.isEmpty() && !error.isEmpty(), "Partial save accepted");
            check(rows(db) == originalRows, "Savepoint did not restore first write");
            check(hot->getId() == originalId && hot->isDirty() && !loop->getId().isValid() && loop->isDirty(), "Failure changed live cue identity/dirty state");
            check(transaction.rollback(), "Outer failure rollback");
        }
        check(q.exec("SELECT baseline FROM import_state") && q.next() && q.value(0) == "old", "Baseline escaped rollback");
        q.finish();
        check(q.exec("DROP TRIGGER reject_insert"), "Remove trigger");
        loop->setLabel("Saved loop");
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error), "Stage successful retry");
            check(staged.size() == 2 && staged[0].get() != hot.get() && staged[1].get() != loop.get(), "Did not detach cues");
            check(staged[0]->getId() == originalId && staged[1]->getId().isValid() && !staged[1]->isDirty(), "Staged cue identities missing");
            check(!loop->getId().isValid() && loop->isDirty() && hot->isDirty(), "Uncommitted save changed original");
            check(transaction.rollback(), "Successful-stage outer rollback");
            staged.clear();
        }
        check(rows(db) == originalRows, "Successful staged rows escaped outer rollback");
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error), "Stage for commit failure");
            check(q.exec("INSERT INTO deferred_child VALUES(999)"), "Deferred failure fixture");
            check(!transaction.commit(), "Expected commit failure");
            check(transaction.rollback(), "Rollback failed commit");
            staged.clear();
        }
        check(rows(db) == originalRows && !loop->getId().isValid() && hot->isDirty(), "Commit failure escaped rollback");
        check(q.exec("CREATE TRIGGER reject_delete BEFORE DELETE ON cues BEGIN SELECT RAISE(ABORT,'injected delete failure'); END"), "Delete trigger");
        {
            SqlTransaction transaction(db);
            check(!dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error), "Orphan deletion failure ignored");
            check(rows(db) == originalRows && staged.isEmpty(), "Deletion failure partially persisted");
        }
        check(q.exec("DROP TRIGGER reject_delete"), "Remove delete trigger");
        check(q.exec("CREATE TRIGGER ignore_update BEFORE UPDATE ON cues BEGIN SELECT RAISE(IGNORE); END"), "Ignored update trigger");
        {
            SqlTransaction transaction(db);
            check(!dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error), "Zero-row update accepted");
            check(rows(db) == originalRows, "Ignored update changed rows");
        }
        check(q.exec("DROP TRIGGER ignore_update"), "Remove update trigger");
        {
            SqlTransaction transaction(db);
            check(!dao.stageTrackCues(transaction, TrackId(QVariant(2)), {hot}, &staged, &error), "Foreign track cue reassigned");
            check(!dao.stageTrackCues(transaction, track, {hot, hot}, &staged, &error), "Duplicate control accepted");
            check(rows(db) == originalRows, "Invalid input changed rows");
        }
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, {hot, loop}, &staged, &error), "Final stage failed");
            check(q.exec("UPDATE import_state SET baseline='accepted'"), "Final baseline write");
            check(transaction.commit(), "Final commit failed");
        }
        check(!loop->getId().isValid() && hot->isDirty(), "Commit auto-mutated original objects");
        auto saved = dao.getCuesForTrack(track);
        check(saved.size() == 2, "Orphan removal or new cue lost");
        for (const auto& cue : saved) {
            check(cue->getEngineOrigin().has_value(), "Source identity lost");
            if (cue->getHotCue() == 26)
                check(cue->getPosition() == F(1000.25) && cue->getEndPosition() == F(2000.75) && cue->getEngineOrigin()->bank == Cue::EngineOrigin::Bank::SavedLoop, "Fractional saved loop changed");
        }
        const auto committedRows = rows(db);
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, staged, &saved, &error), "Idempotent staged save failed");
            check(transaction.commit(), "Idempotent commit failed");
        }
        check(rows(db) == committedRows, "Retry created duplicates");
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, {}, &saved, &error) && saved.isEmpty(), "Empty cue list failed");
            check(rows(db).isEmpty(), "Empty desired set retained rows");
            check(transaction.rollback(), "Empty-stage rollback");
        }
        check(rows(db) == committedRows, "Empty-stage rollback lost rows");
        {
            SqlTransaction transaction(db);
            check(dao.stageTrackCues(transaction, track, staged, &staged, &error), "Aliased input/output failed");
            check(staged.size() == 2 && rows(db) == committedRows, "Aliased output cleared desired cues");
            check(transaction.commit(), "Alias commit");
            check(!dao.stageTrackCues(transaction, track, staged, &saved, &error) && saved.isEmpty(), "Inactive transaction accepted");
        }
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0) == "ok", "Integrity failed");
        std::cout << "PASS staged CueDAO: detached IDs/dirty state, insert/delete/ignored-update/commit failures, savepoint and outer rollback, source banks, fractions, retry/no duplicates, empty desired set\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
