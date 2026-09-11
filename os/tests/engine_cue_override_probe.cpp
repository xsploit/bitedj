#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "library/dao/fscueoverridestore.h"
#include "track/track.h"
using namespace mixxx;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
TrackPointer makeTrack(const QString& path = "/nonexistent/synthetic.wav") {
    auto result = Track::newTemporary(FileAccess(FileInfo(path)));
    result->setAudioProperties(audio::ChannelCount(2), audio::SampleRate(44100), audio::Bitrate(), Duration::fromSeconds(3));
    return result;
}
CuePointer cue(TrackPointer t, int control, bool loop, bool origin = true) {
    auto result = t->createAndAddCue(loop ? CueType::Loop : CueType::HotCue, control, audio::FramePos(loop ? 44100.25 : 22050.5), audio::kInvalidFramePos, RgbColor(loop ? 0x123456 : 0xabcdef));
    if (loop)
        result->setEndPosition(audio::FramePos(88200.75));
    result->setLabel(loop ? "Loop 1" : "Hot 1");
    if (origin)
        result->setEngineOrigin(Cue::EngineOrigin{"engine-source", "9007199254740993", loop ? Cue::EngineOrigin::Bank::SavedLoop : Cue::EngineOrigin::Bank::HotCue, 1});
    return result;
}
CuePointer find(TrackPointer t, int control) {
    for (const auto& c : t->getCuePoints())
        if (c->getHotCue() == control)
            return c;
    return {};
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        auto source = makeTrack();
        auto hot = cue(source, 0, false);
        auto loop = cue(source, 26, true);
        check(!hot->getEndPosition().isValid(), "fixture hot cue must have unset end");
        const auto payload = FsCueOverrideStore::serializeCues(*source);
        auto destination = makeTrack();
        FsCueOverrideStore::applyPayload(destination.get(), payload);
        auto loadedHot = find(destination, 0);
        auto loadedLoop = find(destination, 26);
        check(loadedHot && !loadedHot->getEndPosition().isValid(), "portable hot cue acquired a valid end");
        check(bool(loadedLoop), "Engine loop at local control26 omitted from portable store");
        check(loadedHot && loadedHot->getEngineOrigin() == hot->getEngineOrigin() && loadedLoop->getEngineOrigin() == loop->getEngineOrigin(), "Engine bank/source identity lost");
        check(std::abs(loadedLoop->getPosition().value() - 44100.25) < 1e-8 && std::abs(loadedLoop->getEndPosition().value() - 88200.75) < 1e-8, "fractional frame roundtrip");
        const auto roundTrip = FsCueOverrideStore::serializeCues(*destination);
        if (roundTrip != payload)
            std::cerr << payload.constData() << "\n"
                      << roundTrip.constData() << "\n";
        check(roundTrip == payload, "portable payload not stable");
        auto* originalPointer = loadedLoop.get();
        loadedLoop->setLabel("Local edit");
        FsCueOverrideStore::applyPayload(destination.get(), payload);
        check(find(destination, 26).get() == originalPointer && loadedLoop->getLabel() == "Loop 1", "existing QObject replaced instead of edited");
        auto legacy = makeTrack();
        auto legacyLoop = cue(legacy, 26, true);
        FsCueOverrideStore::applyPayload(legacy.get(), "[]");
        check(find(legacy, 26) == legacyLoop, "legacy empty payload erased newer bank");
        auto empty = makeTrack();
        FsCueOverrideStore::applyPayload(destination.get(), FsCueOverrideStore::serializeCues(*empty));
        check(!find(destination, 0) && !find(destination, 26), "new empty snapshot did not remove source cues");
        auto custom = makeTrack();
        auto customLoop = cue(custom, 26, true, false);
        const auto before = customLoop->getLabel();
        FsCueOverrideStore::applyPayload(custom.get(), payload);
        check(find(custom, 26) == customLoop && !customLoop->getEngineOrigin() && customLoop->getLabel() == before && !find(custom, 0), "payload overwrote occupied custom control or partially applied");
        auto ordinary = makeTrack();
        cue(ordinary, 3, true, false);
        cue(ordinary, 17, false, false);
        ordinary->setMainCuePosition(audio::FramePos(1234.5));
        ordinary->createAndAddCue(CueType::Intro, Cue::kNoHotCue, audio::FramePos(20), audio::FramePos(100));
        auto ordinaryLoaded = makeTrack();
        auto intro = ordinaryLoaded->createAndAddCue(CueType::Intro, Cue::kNoHotCue, audio::FramePos(10), audio::FramePos(90));
        FsCueOverrideStore::applyPayload(ordinaryLoaded.get(), FsCueOverrideStore::serializeCues(*ordinary));
        check(find(ordinaryLoaded, 3) && find(ordinaryLoaded, 17) && ordinaryLoaded->getMainCuePosition().value() == 1234.5 &&
                        ordinaryLoaded->findCueByType(CueType::Intro) == intro && intro->getPosition().value() == 10,
                "ordinary pad/memory/main/intro regression");
        auto oldFormat = makeTrack();
        auto oldHot = cue(oldFormat, 0, false);
        FsCueOverrideStore::applyPayload(oldFormat.get(), R"([{"slot":0,"type":1,"pos":1,"color":123}])");
        check(find(oldFormat, 0) == oldHot && oldHot->getEngineOrigin() == hot->getEngineOrigin() && oldHot->getPosition().value() == 44100, "legacy in-place origin compatibility");
        auto plain = makeTrack();
        cue(plain, 0, false, false);
        FsCueOverrideStore::applyPayload(oldFormat.get(), FsCueOverrideStore::serializeCues(*plain));
        check(!oldHot->getEngineOrigin(), "explicit plain replacement retained stale source identity");
        auto document = QJsonDocument::fromJson(payload).object();
        auto values = document["cues"].toArray();
        auto invalid = values[0].toObject();
        invalid["engineOrigin"] = QJsonObject{{"libraryUuid", "engine-source"}, {"trackId", "1"}, {"bank", 2}, {"slot", 99}};
        values[0] = invalid;
        document["cues"] = values;
        auto invalidTarget = makeTrack();
        auto retained = cue(invalidTarget, 0, false);
        const auto intact = FsCueOverrideStore::serializeCues(*invalidTarget);
        FsCueOverrideStore::applyPayload(invalidTarget.get(), QJsonDocument(document).toJson());
        check(FsCueOverrideStore::serializeCues(*invalidTarget) == intact && find(invalidTarget, 0) == retained, "malformed identity partially applied");
        document["version"] = 99;
        FsCueOverrideStore::applyPayload(invalidTarget.get(), QJsonDocument(document).toJson());
        check(FsCueOverrideStore::serializeCues(*invalidTarget) == intact, "future payload applied");
        if (argc == 2 && QString(argv[1]) == "--store") {
            const QString location = "/mnt/usbtest/synthetic.wav";
            QFile media(location);
            check(media.open(QIODevice::WriteOnly), "isolated media file");
            media.close();
            check(QDir().mkpath("/mnt/usbtest/.bitedj"), "isolated store directory");
            auto db = QSqlDatabase::addDatabase("QSQLITE", "portable-store-test");
            db.setDatabaseName("/mnt/usbtest/.bitedj/cues.sqlite");
            check(db.open(), "test store open");
            QSqlQuery q(db);
            check(q.exec("CREATE TABLE cue_overrides(relpath TEXT PRIMARY KEY,version INTEGER,updated_at TEXT,cues TEXT)"), "old schema");
            q.prepare("INSERT INTO cue_overrides VALUES('synthetic.wav',1,'old',:payload)");
            q.bindValue(":payload", QString(R"([{"slot":0,"type":1,"pos":1,"color":123}])"));
            check(q.exec(), "old row seed");
            q.finish();
            auto oldTrack = makeTrack(location);
            auto oldTrackHot = cue(oldTrack, 0, false);
            auto oldTrackLoop = cue(oldTrack, 26, true);
            FsCueOverrideStore::applyOverrides(oldTrack.get());
            check(oldTrackHot->getPosition().value() == 44100 && oldTrackHot->getEngineOrigin() == hot->getEngineOrigin() && find(oldTrack, 26) == oldTrackLoop, "version1 database read");
            FsCueOverrideStore::flushIfChanged(*oldTrack);
            check(q.exec("SELECT version,updated_at FROM cue_overrides") && q.next() && q.value(0).toInt() == 1 && q.value(1) == "old", "unchanged load wrote portable database");
            q.finish();
            oldTrackLoop->setLabel("Portable edit");
            FsCueOverrideStore::flushIfChanged(*oldTrack);
            check(q.exec("SELECT version,cues FROM cue_overrides") && q.next() && q.value(0).toInt() == 2 && QJsonDocument::fromJson(q.value(1).toByteArray()).isObject(), "edited store not upgraded to version2");
            q.finish();
            auto fresh = makeTrack(location);
            FsCueOverrideStore::applyOverrides(fresh.get());
            check(find(fresh, 26) && find(fresh, 26)->getLabel() == "Portable edit" && find(fresh, 26)->getEngineOrigin() == loop->getEngineOrigin(), "version2 fresh-unit reload");
            auto imported = makeTrack(location);
            auto importedLoop = cue(imported, 26, true);
            importedLoop->setLabel("Imported source label");
            FsCueOverrideStore::applyOverrides(imported.get());
            check(importedLoop->getLabel() == "Portable edit", "override did not apply in place");
            check(FsCueOverrideStore::restoreImportedCues(imported.get()) && find(imported, 26) == importedLoop && importedLoop->getLabel() == "Imported source label" && importedLoop->getEngineOrigin() == loop->getEngineOrigin(), "clear restoration lost source identity");
            check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0) == "ok", "portable DB integrity");
            std::cout << "PASS isolated removable-filesystem store: version1 read/no-op save, version2 write/fresh reload, imported-cue restoration and integrity\n";
        }
        std::cout << "PASS portable Engine cues: separate banks/source IDs/fractions, stable serialization, in-place update, legacy compatibility, scoped deletion, custom-control protection and malformed/future rejection\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
