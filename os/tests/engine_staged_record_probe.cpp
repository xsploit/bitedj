#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "database/schemamanager.h"
#include "library/dao/cuedao.h"
#include "library/dao/trackdao.h"
#include "library/engine/engineimportregistry.h"
#include "library/engine/enginemetadataimport.h"
#include "track/beats.h"
#include "util/db/sqltransaction.h"
#include "util/fileinfo.h"
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static QVariantList row(QSqlDatabase db, const QString& table) {
    QSqlQuery q(db);
    check(q.exec("SELECT * FROM " + table + " WHERE id=1") && q.next(), "Read row");
    QVariantList result;
    for (int i = 0; i < q.record().count(); ++i)
        result.append(q.value(i));
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check(argc == 2, "Expected schema XML argument");
        QTemporaryDir temp;
        check(temp.isValid(), "Temporary directory");
        auto db = QSqlDatabase::addDatabase("QSQLITE", "staged-record");
        db.setDatabaseName(temp.filePath("library.sqlite"));
        check(db.open(), "Open DB");
        SchemaManager schema(db);
        check(schema.upgradeToSchemaVersion(41, argv[1]) == SchemaManager::Result::UpgradeSucceeded, "Real schema41 creation");
        QSqlQuery q(db);
        check(q.exec("INSERT INTO track_locations(id,location,directory,filename) VALUES(1,'/nonexistent/synthetic.wav','/nonexistent','synthetic.wav')"), "Location fixture");
        check(q.exec("INSERT INTO library(id,location,title,datetime_added) VALUES(1,1,'Old','2020-01-01')"), "Library fixture");
        const auto before = row(db, "library");
        mixxx::TrackRecord original(TrackId(QVariant(1)));
        original.refMetadata().refTrackInfo().setTitle("Old");
        original.refMetadata().refStreamInfo().setDuration(mixxx::Duration::fromSeconds(3));
        auto plan = mixxx::planEngineMetadataImport(original, {{"title", "Old"}}, {{"sourceTitle", "New"}, {"keyId", 1}, {"ratingPercent", 60}});
        auto beats = mixxx::Beats::fromConstTempo(mixxx::audio::SampleRate(44100), mixxx::audio::FramePos(0), mixxx::Bpm(120));
        auto cue = CuePointer(new Cue(mixxx::CueType::Loop, 26, mixxx::audio::FramePos(1000.25), mixxx::audio::FramePos(2000.75), mixxx::RgbColor(0xabcdef)));
        cue->setEngineOrigin(Cue::EngineOrigin{"source", "1", Cue::EngineOrigin::Bank::SavedLoop, 1});
        CueDAO cueDao;
        cueDao.initialize(db);
        mixxx::EngineImportRegistry registry(db);
        QString error;
        QList<CuePointer> staged;
        check(q.exec("CREATE TRIGGER reject_baseline BEFORE INSERT ON engine_import_entities BEGIN SELECT RAISE(ABORT,'injected baseline failure'); END"), "Registry trigger");
        {
            SqlTransaction transaction(db);
            check(TrackDAO::stageTrackRecord(transaction, plan.record, beats, &error), "Stage record before failure");
            check(cueDao.stageTrackCues(transaction, original.getId(), {cue}, &staged, &error), "Stage cue before failure");
            check(!registry.save({"source", "track", "1"}, {1, plan.baseline}, &error), "Expected registry failure");
            check(transaction.rollback(), "Outer rollback");
            staged.clear();
        }
        check(row(db, "library") == before, "Record escaped combined rollback");
        check(cueDao.getCuesForTrack(original.getId()).isEmpty() && !cue->getId().isValid() && cue->isDirty(), "Cue escaped combined rollback");
        check(original.getMetadata().getTrackInfo().getTitle() == "Old", "Original metadata changed");
        check(q.exec("DROP TRIGGER reject_baseline"), "Remove trigger");
        {
            SqlTransaction transaction(db);
            check(TrackDAO::stageTrackRecord(transaction, plan.record, beats, &error), "Stage committed record");
            check(cueDao.stageTrackCues(transaction, original.getId(), {cue}, &staged, &error), "Stage committed cues");
            check(registry.save({"source", "track", "1"}, {1, plan.baseline}, &error), "Stage committed baseline");
            check(transaction.commit(), "Combined commit");
        }
        check(q.exec("SELECT title,rating,key_id,location,datetime_added,bpm FROM library WHERE id=1") && q.next(), "Read committed fields");
        check(q.value(0) == "New" && q.value(1).toInt() == 3 && q.value(2).toInt() == mixxx::track::io::key::A_MINOR, "Committed metadata wrong");
        check(q.value(3).toInt() == 1 && q.value(4) == "2020-01-01" && q.value(5).toDouble() == 120, "Location/date/beat serialization wrong");
        q.finish();
        auto loaded = cueDao.getCuesForTrack(original.getId());
        check(loaded.size() == 1 && loaded[0]->getEndPosition() == mixxx::audio::FramePos(2000.75), "Committed cue wrong");
        std::optional<mixxx::EngineImportRecord> baseline;
        check(registry.load({"source", "track", "1"}, &baseline, &error) && baseline && baseline->baseline == plan.baseline, "Committed baseline wrong");
        const auto committed = row(db, "library");
        {
            SqlTransaction transaction(db);
            auto missing = plan.record;
            missing.setId(TrackId(QVariant(999)));
            check(!TrackDAO::stageTrackRecord(transaction, missing, beats, &error) && !error.isEmpty(), "Missing track accepted");
        }
        check(q.exec("CREATE TRIGGER reject_record BEFORE UPDATE ON library BEGIN SELECT RAISE(ABORT,'injected track failure'); END"), "Track failure trigger");
        {
            SqlTransaction transaction(db);
            check(!TrackDAO::stageTrackRecord(transaction, plan.record, beats, &error) && !error.isEmpty(), "Failed track update accepted");
        }
        check(row(db, "library") == committed, "Rejected track update changed data");
        check(q.exec("DROP TRIGGER reject_record"), "Remove track trigger");
        {
            SqlTransaction transaction(db);
            check(TrackDAO::stageTrackRecord(transaction, plan.record, beats, &error), "Repeated record save");
            check(cueDao.stageTrackCues(transaction, original.getId(), staged, &loaded, &error), "Repeated cue save");
            check(registry.save({"source", "track", "1"}, {1, plan.baseline}, &error) && transaction.commit(), "Repeated transaction commit");
        }
        check(row(db, "library") == committed && cueDao.getCuesForTrack(original.getId()).size() == 1, "Repeated transaction changed identities");
        check(q.exec("SELECT count(*) FROM engine_import_entities") && q.next() && q.value(0).toInt() == 1, "Repeated registry duplicated");
        q.finish();
        QFile file(temp.filePath("new.wav"));
        check(file.open(QIODevice::WriteOnly) && file.write("synthetic database fixture") > 0, "Create file-info fixture");
        file.close();
        const mixxx::FileInfo fileInfo(file);
        mixxx::TrackRecord newRecord;
        newRecord.refMetadata().refTrackInfo().setTitle("New file record");
        mixxx::TrackRecord inserted;
        check(q.exec("CREATE TRIGGER reject_new_track BEFORE INSERT ON library BEGIN SELECT RAISE(ABORT,'injected new-track failure'); END"), "New track trigger");
        {
            SqlTransaction transaction(db);
            check(!TrackDAO::stageNewTrackRecord(transaction, newRecord, {}, fileInfo, &inserted, &error) && !inserted.getId().isValid(), "Rejected insert accepted");
            check(q.exec("SELECT count(*) FROM track_locations") && q.next() && q.value(0).toInt() == 1, "Failed insert left file location");
            q.finish();
        }
        check(q.exec("DROP TRIGGER reject_new_track"), "Remove insert trigger");
        {
            SqlTransaction transaction(db);
            check(TrackDAO::stageNewTrackRecord(transaction, newRecord, {}, fileInfo, &inserted, &error), "Stage new track for rollback");
            check(inserted.getId().isValid() && !newRecord.getId().isValid(), "New staging changed input identity");
            check(transaction.rollback(), "New record outer rollback");
        }
        check(q.exec("SELECT count(*) FROM library") && q.next() && q.value(0).toInt() == 1, "New record escaped rollback");
        q.finish();
        {
            SqlTransaction transaction(db);
            check(TrackDAO::stageNewTrackRecord(transaction, newRecord, {}, fileInfo, &inserted, &error), "New record retry failed");
            check(registry.save({"source", "track", "new"}, {inserted.getId().toVariant().toLongLong(), {{"title", "New file record"}}}, &error), "New source mapping failed");
            check(transaction.commit(), "New record commit failed");
        }
        const auto insertedId = inserted.getId();
        {
            SqlTransaction transaction(db);
            check(!TrackDAO::stageNewTrackRecord(transaction, newRecord, {}, fileInfo, &inserted, &error), "Duplicate file inserted");
            check(!inserted.getId().isValid(), "Failed insert retained stale output identity");
        }
        check(q.exec("SELECT count(*) FROM library") && q.next() && q.value(0).toInt() == 2, "Duplicate/failed insert changed row count");
        q.finish();
        check(registry.load({"source", "track", "new"}, &baseline, &error) && baseline && baseline->localId == insertedId.toVariant().toLongLong(), "New source identity lost");
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0) == "ok", "Database integrity");
        std::cout << "PASS actual schema41 TrackDAO+CueDAO+registry transaction: rollback on final registry failure, commit/retry, metadata/beats/frames/source identity, missing/rejected record, no original-object mutation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
