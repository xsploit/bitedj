#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlDriver>
#include <sqlite3.h>
#include <cstring>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include "database/schemamanager.h"
#include "library/dao/cuedao.h"
#include "util/db/sqltransaction.h"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static CuePointer makeCue(int control, const QString& label) {
    CuePointer cue(new Cue(mixxx::CueType::Loop, control,
            mixxx::audio::FramePos(100.25), mixxx::audio::FramePos(200.75),
            mixxx::RgbColor(0x123456)));
    cue->setLabel(label);
    return cue;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check(argc == 2, "expected schema XML");
        QTemporaryDir temp;
        auto db = QSqlDatabase::addDatabase("QSQLITE", "cue-save-commit");
        db.setDatabaseName(temp.filePath("library.sqlite"));
        check(db.open(), "database open");
        SchemaManager schema(db);
        check(schema.upgradeToSchemaVersion(42, argv[1]) == SchemaManager::Result::UpgradeSucceeded, "schema42");
        QSqlQuery q(db);
        auto sql = [&](const QString& text) { check(q.exec(text), "fixture SQL"); };
        auto scalar = [&](const QString& text) { sql(text); check(q.next(), "query result"); return q.value(0); };
        sql("INSERT INTO library(id,title) VALUES(1,'before')");
        CueDAO dao; dao.initialize(db);
        const TrackId trackId(QVariant(1));
        QString error;
        auto first = makeCue(26, "original");
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            check(dao.prepareTrackCueSave(tx, trackId, {first}, &pending, &error), "initial prepare");
            check(!first->getId().isValid() && first->isDirty(), "prepare changed original identity/state");
            check(tx.commit() && dao.finishTrackCueSave(&pending), "initial commit");
        }
        const auto firstId = first->getId();
        check(firstId.isValid() && !first->isDirty(), "initial accepted identity");
        sql("CREATE TRIGGER reject_cue BEFORE UPDATE OF label ON cues WHEN NEW.label='rejected' BEGIN SELECT RAISE(FAIL,'injected failure'); END");
        auto added = makeCue(27, "new"); first->setLabel("rejected");
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            sql("UPDATE library SET title='must roll back' WHERE id=1");
            check(!dao.prepareTrackCueSave(tx, trackId, {added, first}, &pending, &error) && !error.isEmpty(), "partial cue failure accepted");
            check(!added->getId().isValid() && added->isDirty() && first->getId() == firstId && first->isDirty(), "partial failure changed live state");
        }
        check(scalar("SELECT count(*) FROM cues").toInt() == 1 && scalar("SELECT title FROM library WHERE id=1").toString() == "before", "partial failure rollback");
        sql("DROP TRIGGER reject_cue");
        auto handle = db.driver()->handle();
        check(handle.isValid() && std::strcmp(handle.typeName(), "sqlite3*") == 0, "native SQLite handle");
        auto* sqlite = *static_cast<sqlite3**>(handle.data());
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            if (!dao.prepareTrackCueSave(tx, trackId, {first, added}, &pending, &error)) throw std::runtime_error("prepare before failed outer commit: " + error.toStdString());
            sqlite3_commit_hook(sqlite, [](void*) { return 1; }, nullptr);
            const bool committed = tx.commit();
            sqlite3_commit_hook(sqlite, nullptr, nullptr);
            check(!committed, "outer commit unexpectedly succeeded");
            check(!added->getId().isValid() && added->isDirty() && first->isDirty(), "failed commit changed original state");
        }
        check(scalar("SELECT count(*) FROM cues").toInt() == 1, "failed commit rollback");
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            check(dao.prepareTrackCueSave(tx, trackId, {first, added}, &pending, &error), "retry prepare");
            added->setLabel("newer live edit");
            check(tx.commit(), "retry commit");
            check(!dao.finishTrackCueSave(&pending), "concurrent edit marked saved");
            check(added->getId().isValid() && added->isDirty() && added->getLabel() == "newer live edit", "lost concurrent edit or committed identity");
        }
        const auto addedId = added->getId();
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            check(dao.prepareTrackCueSave(tx, trackId, {first, added}, &pending, &error) && tx.commit() && dao.finishTrackCueSave(&pending), "save newer edit");
        }
        check(added->getId() == addedId && first->getId() == firstId && !added->isDirty(), "retry identity/state");
        check(scalar("SELECT count(*) FROM cues").toInt() == 2 && scalar("SELECT label FROM cues WHERE hotcue=27").toString() == "newer live edit", "retry duplicated/lost row");
        sql("CREATE TRIGGER reject_delete BEFORE DELETE ON cues BEGIN SELECT RAISE(FAIL,'injected delete failure'); END");
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            check(!dao.prepareTrackCueSave(tx, trackId, {first}, &pending, &error), "delete failure accepted");
        }
        check(scalar("SELECT count(*) FROM cues").toInt() == 2, "delete failure rollback");
        sql("DROP TRIGGER reject_delete");
        first->setStartAndEndPosition(mixxx::audio::kInvalidFramePos, mixxx::audio::kInvalidFramePos);
        {
            SqlTransaction tx(db); CueDAO::PendingCueSave pending;
            check(dao.prepareTrackCueSave(tx, trackId, {first}, &pending, &error) && tx.commit() && dao.finishTrackCueSave(&pending), "cleared bounds and deletion");
        }
        check(!first->isDirty() && scalar("SELECT count(*) FROM cues").toInt() == 1, "final save state");
        std::cout << "PASS cue save: partial write/delete failure, outer commit failure, unchanged IDs/dirty flags on rollback, concurrent edit retained, retry without duplicate IDs, cleared bounds\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
