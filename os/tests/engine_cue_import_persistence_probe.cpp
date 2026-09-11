#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "database/schemamanager.h"
#include "library/dao/cuedao.h"
#include "library/engine/enginecueimport.h"
#include "library/engine/engineimportpackage.h"
#include "library/engine/engineimportregistry.h"
#include "util/db/sqltransaction.h"
using namespace mixxx;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject readPackage(const char* path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "open package");
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    check(parseError.error == QJsonParseError::NoError && doc.isObject(), "package JSON");
    QString error;
    check(validateEngineImportPackage(doc.object(), &error), "validated package");
    return doc.object();
}
QVector<EngineCueImportItem> snapshots(const QList<CuePointer>& cues) {
    QVector<EngineCueImportItem> result;
    // These are detached DAO results in this single-threaded probe, not live
    // player objects. A production caller must capture a consistent live view.
    for (const auto& cue : cues) {
        result.append({cue->getId().toVariant().toLongLong(), cue->getHotCue(), cue->getType(), cue->getPosition().value(), cue->getEndPosition().isValid() ? cue->getEndPosition().value() : Cue::kNoPosition, cue->getLabel(), static_cast<quint32>(cue->getColor()), cue->getEngineOrigin()});
    }
    return result;
}
QList<CuePointer> materialize(const QVector<EngineCueImportItem>& items) {
    QList<CuePointer> result;
    using F = audio::FramePos;
    for (const auto& item : items) {
        CuePointer cue;
        if (item.databaseId) {
            cue = CuePointer(new Cue(DbId(QVariant(item.databaseId)), item.type, F(item.startFrame), item.endFrame == Cue::kNoPosition ? 0 : item.endFrame - item.startFrame, item.control, item.label, RgbColor(item.rgb), item.origin));
        } else {
            cue = CuePointer(new Cue(item.type, item.control, F(item.startFrame), audio::kInvalidFramePos, RgbColor(item.rgb)));
            if (item.endFrame != Cue::kNoPosition)
                cue->setEndPosition(F(item.endFrame));
            cue->setLabel(item.label);
            cue->setEngineOrigin(item.origin);
        }
        result.append(cue);
    }
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check(argc == 4, "Expected schema XML, initial package and updated package");
        const auto initial = readPackage(argv[2]);
        const auto updated = readPackage(argv[3]);
        check(initial["sourceUuid"] == updated["sourceUuid"], "stable library identity");
        const auto first = initial["tracks"].toArray()[0].toObject();
        const auto next = updated["tracks"].toArray()[0].toObject();
        check(first["id"] == next["id"] && first["sampleRate"].toDouble() == 44100 &&
                        first["sampleCount"].toString() == "132300",
                "expected exact WAV fixture frame domain");
        QTemporaryDir temp;
        check(temp.isValid(), "temporary directory");
        auto db = QSqlDatabase::addDatabase("QSQLITE", "cue-merge-fixture");
        db.setDatabaseName(temp.filePath("library.sqlite"));
        check(db.open(), "database open");
        SchemaManager schema(db);
        check(schema.upgradeToSchemaVersion(42, argv[1]) == SchemaManager::Result::UpgradeSucceeded, "schema42");
        QSqlQuery q(db);
        check(q.exec("INSERT INTO library(id,title) VALUES(1,'Synthetic cue test')"), "local track row");
        CueDAO dao;
        dao.initialize(db);
        EngineImportRegistry registry(db);
        const TrackId track(QVariant(1));
        const EngineImportKey key{initial["sourceUuid"].toString(), "cue", "track:1:bank-baselines"};
        QString error;
        QJsonObject baseline;
        QVector<EngineCueImportItem> current;
        QList<CuePointer> staged;
        auto apply = [&](const QJsonObject& source, bool rollback) {
            auto plan = planEngineCueImport(current, baseline, key.libraryUuid, first["id"].toString(), source, 132300, {0, 1, 2, 3, 4, 5, 6, 7}, {26, 27, 28, 29, 30, 31, 32, 33});
            check(plan.error.isEmpty() && plan.conflicts.isEmpty() && plan.unrepresentableFields.isEmpty(), "cue plan");
            SqlTransaction tx(db);
            check(dao.stageTrackCues(tx, track, materialize(plan.cues), &staged, &error), "stage planned cues");
            check(registry.save(key, {1, plan.baseline}, &error), "stage baseline");
            if (rollback) {
                check(tx.rollback(), "rollback plan");
                staged.clear();
                return;
            }
            check(tx.commit(), "commit plan");
            baseline = plan.baseline;
            current = snapshots(dao.getCuesForTrack(track));
            check(current.size() == 3, "hot1 plus loop1 and loop8");
            std::optional<EngineImportRecord> loaded;
            check(registry.load(key, &loaded, &error) && loaded && loaded->baseline == baseline, "reload baseline");
        };
        apply(first, false);
        const auto ids = [&]() { QList<qint64> result; for (const auto& cue : current) result.append(cue.databaseId); return result; }();
        const auto oldBaseline = baseline;
        apply(next, true);
        std::optional<EngineImportRecord> rolledBack;
        check(registry.load(key, &rolledBack, &error) && rolledBack && rolledBack->baseline == oldBaseline, "rollback baseline unchanged");
        auto oldCues = snapshots(dao.getCuesForTrack(track));
        check(oldCues[0].startFrame == current[0].startFrame && oldCues[1].startFrame == current[1].startFrame, "rollback persisted cue positions");
        apply(next, false);
        apply(next, false);
        QList<qint64> reimportIds;
        bool hot = false, loop = false, end = false;
        for (const auto& cue : current) {
            reimportIds.append(cue.databaseId);
            check(cue.origin && cue.origin->trackId == "1" && cue.origin->libraryUuid == key.libraryUuid, "persisted provenance");
            if (cue.origin->bank == Cue::EngineOrigin::Bank::HotCue)
                hot = cue.origin->slot == 1 && cue.startFrame == 33075.5 && cue.label == "Cue B" && cue.endFrame == Cue::kNoPosition;
            else if (cue.origin->slot == 1)
                loop = cue.startFrame == 55125.25 && cue.endFrame == 99225.75 && cue.label == "Loop B";
            else if (cue.origin->slot == 8)
                end = cue.endFrame == 132300;
        }
        check(reimportIds == ids && hot && loop && end, "stable IDs and updated exact bank values");
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0) == "ok", "integrity");
        std::cout << "PASS validated WAV reader packages -> cue planner -> actual CueDAO+registry: initial/update/rollback/retry/reload, stable IDs, separate banks and exact fractional frames\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
