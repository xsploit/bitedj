#include <QCoreApplication>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "library/engine/enginemetadataimport.h"
#include "track/track.h"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        using Result = Track::RecordReplaceResult;
        auto track = Track::newTemporary();
        track->setAudioProperties(mixxx::audio::ChannelCount(2),
                mixxx::audio::SampleRate(44100), mixxx::audio::Bitrate(),
                mixxx::Duration::fromSeconds(60));
        check(track->trySetBpm(mixxx::Bpm(128)), "beat fixture");
        const auto beats = track->getBeats();
        check(bool(beats), "missing beat fixture");
        const auto beatBytes = beats->toByteArray();
        const auto cue = track->createAndAddCue(mixxx::CueType::Loop, 26,
                mixxx::audio::FramePos(44100.25), mixxx::audio::FramePos(88200.75));
        cue->setEngineOrigin(Cue::EngineOrigin{"source", "track-1", Cue::EngineOrigin::Bank::SavedLoop, 1});
        cue->setLabel("My loop");
        track->setTitle("Original");
        track->markClean();
        std::atomic<int> titleSignals{0};
        QObject::connect(track.get(), &Track::titleChanged, [&](const QString&) { ++titleSignals; });
        const auto expected = track->getRecord();
        const auto plan = mixxx::planEngineMetadataImport(expected, {},
                QJsonObject{{"sourceTitle", "Imported"}}, true);
        check(track->replaceRecordIfUnchanged(expected, plan.record) == Result::Updated,
                "accepted publication");
        check(track->getTitle() == "Imported" && track->isDirty() && titleSignals == 1,
                "accepted state/signals");
        check(track->getBeats() == beats && beats->toByteArray() == beatBytes &&
                track->getCuePoints().contains(cue) && cue->getLabel() == "My loop" &&
                cue->getPosition() == mixxx::audio::FramePos(44100.25) &&
                cue->getEndPosition() == mixxx::audio::FramePos(88200.75),
                "metadata publication changed timing or cues");
        track->markClean();
        const auto current = track->getRecord();
        const int beforeNoop = titleSignals;
        check(track->replaceRecordIfUnchanged(current, current) == Result::Unchanged &&
                !track->isDirty() && titleSignals == beforeNoop, "no-op dirtied or signaled");
        auto pending = current;
        pending.refMetadata().refTrackInfo().setTitle("Stale import");
        std::thread editor([&] { track->setTitle("Local edit"); });
        editor.join();
        const int beforeStale = titleSignals;
        check(track->replaceRecordIfUnchanged(current, pending) == Result::Stale &&
                track->getTitle() == "Local edit" && track->isDirty() &&
                titleSignals == beforeStale && track->getBeats() == beats,
                "stale publication overwrote local edit or signaled");
        track->markClean();
        check(track->replaceRecordIfUnchanged(current, pending) == Result::Stale &&
                !track->isDirty(), "rejected publication dirtied a clean track");
        // A cue-only edit is outside TrackRecord and must survive metadata CAS.
        auto cueOnlySnapshot = track->getRecord();
        auto cueOnlyTarget = cueOnlySnapshot;
        cueOnlyTarget.refMetadata().refTrackInfo().setArtist("Imported artist");
        cue->setLabel("Later local loop");
        check(track->replaceRecordIfUnchanged(cueOnlySnapshot, cueOnlyTarget) == Result::Updated &&
                cue->getLabel() == "Later local loop", "cue-only edit lost");
        for (int round = 0; round < 64; ++round) {
            const auto snapshot = track->getRecord();
            auto a = snapshot, b = snapshot;
            a.refMetadata().refTrackInfo().setTitle(QString("A%1").arg(round));
            b.refMetadata().refTrackInfo().setTitle(QString("B%1").arg(round));
            std::atomic<int> ready{0};
            std::atomic<bool> go{false};
            Result ra = Result::Unchanged, rb = Result::Unchanged;
            const auto run = [&](const mixxx::TrackRecord& target, Result* result) {
                ++ready;
                while (!go.load()) std::this_thread::yield();
                *result = track->replaceRecordIfUnchanged(snapshot, target);
            };
            std::thread first(run, std::cref(a), &ra), second(run, std::cref(b), &rb);
            while (ready.load() != 2) std::this_thread::yield();
            go = true;
            first.join(); second.join();
            check((ra == Result::Updated && rb == Result::Stale) ||
                    (rb == Result::Updated && ra == Result::Stale), "two stale competing snapshots published");
        }
        auto legacy = track->getRecord();
        legacy.refMetadata().refTrackInfo().setTitle("Legacy setter");
        check(track->replaceRecord(legacy) && !track->replaceRecord(legacy), "legacy bool behavior changed");
        check(track->getBeats() == beats && beats->toByteArray() == beatBytes &&
                cue->getLabel() == "Later local loop", "publication race altered beats/cues");
        std::cout << "PASS live Track publication: accepted/no-op/stale, signals/dirty state, metadata planner, preserved beat grid/cues, 64 competing publications and legacy setter.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
