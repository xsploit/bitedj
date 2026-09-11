#include <sqlite3.h>

#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <iostream>

#include "database/schemamanager.h"
// Diagnostic-only access to the existing batch methods; no production header edits.
#define private public
#include "library/dao/trackdao.h"
#undef private
#include "library/dao/analysisdao.h"
#include "library/dao/cuedao.h"
#include "library/dao/libraryhashdao.h"
#include "library/dao/playlistdao.h"
#include "track/track.h"
struct Saver final : GlobalTrackCacheSaver {
    void saveEvictedTrack(Track*) noexcept override {
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    Saver saver;
    GlobalTrackCache::createInstance(&saver);
    QTemporaryDir temp;
    auto config = UserSettingsPointer::create(temp.filePath("mixxx.cfg"), QString(), temp.path());
    config->setValue(ConfigKey("[Library]", "AnalysisCacheOnTrackFs"), false);
    config->setValue(ConfigKey("[Library]", "AnalysisCacheInHome"), false);
    auto db = QSqlDatabase::addDatabase("QSQLITE", "new-track-commit");
    db.setDatabaseName(temp.filePath("library.sqlite"));
    if (!db.open())
        return 3;
    SchemaManager schema(db);
    if (schema.upgradeToSchemaVersion(42, argv[1]) != SchemaManager::Result::UpgradeSucceeded)
        return 4;
    QFile media(temp.filePath("synthetic.wav"));
    if (!media.open(QIODevice::WriteOnly))
        return 5;
    media.write(QByteArray::fromHex("524946462400000057415645666d7420100000000100020044ac000010b10200040010006461746100000000"));
    media.close();
    CueDAO cues;
    cues.initialize(db);
    PlaylistDAO playlists;
    playlists.initialize(db);
    AnalysisDao analysis(config);
    analysis.initialize(db);
    LibraryHashDAO hashes;
    hashes.initialize(db);
    QSet<TrackId> announced;
    QList<TrackPointer> committedTracks;
    TrackDAO tracks(cues, playlists, analysis, hashes, config);
    tracks.initialize(db);
    QObject::connect(&tracks, &TrackDAO::tracksAdded, [&](const QSet<TrackId>& ids) { announced.unite(ids); });
    GlobalTrackCacheResolver resolver{mixxx::FileAccess(mixxx::FileInfo(media.fileName()))};
    auto track = resolver.getTrack();
    track->setAudioProperties(mixxx::audio::ChannelCount(2), mixxx::audio::SampleRate(44100), mixxx::audio::Bitrate(), mixxx::Duration::fromSeconds(1));
    auto cue = track->createAndAddCue(mixxx::CueType::HotCue, 0, mixxx::audio::FramePos(100), mixxx::audio::kInvalidFramePos);
    tracks.addTracksPrepare();
    const auto id = tracks.addTracksAddTrack(track, true);
    if (!id.isValid())
        return 6;
    resolver.initTrackIdAndUnlockCache(id);
    {
        GlobalTrackCacheLocker cache;
        if (cache.lookupTrackById(id) != track)
            return 12;
    }
    auto handle = db.driver()->handle();
    if (!handle.isValid() || QByteArray(handle.typeName()) != "sqlite3*")
        return 30;
    auto* sqlite = *static_cast<sqlite3**>(handle.data());
    if (!sqlite)
        return 31;
    sqlite3_commit_hook(sqlite, [](void*) { return 1; }, nullptr);
    const bool committed = tracks.addTracksFinish(false, &committedTracks);
    sqlite3_commit_hook(sqlite, nullptr, nullptr);
    QSqlQuery q(db);
    if (!q.exec("SELECT count(*) FROM library") || !q.next())
        return 7;
    if (!committedTracks.isEmpty())
        return 26;
    if (committed || q.value(0).toInt() != 0 || !announced.isEmpty() || track->getId().isValid() || cue->getId().isValid() || !cue->isDirty() || !track->isDirty())
        return 8;
    {
        GlobalTrackCacheLocker cache;
        if (cache.lookupTrackById(id))
            return 13;
    }
    GlobalTrackCacheResolver retryResolver{mixxx::FileAccess(mixxx::FileInfo(media.fileName()))};
    if (retryResolver.getTrack() != track || retryResolver.getLookupResult() != GlobalTrackCacheLookupResult::Hit)
        return 14;
    tracks.addTracksPrepare();
    const auto retryId = tracks.addTracksAddTrack(track, true);
    if (!retryId.isValid())
        return 9;
    retryResolver.initTrackIdAndUnlockCache(retryId);
    if (!tracks.addTracksFinish(false, &committedTracks))
        return 9;
    {
        GlobalTrackCacheLocker cache;
        if (cache.lookupTrackById(retryId) != track)
            return 15;
    }
    if (committedTracks != QList<TrackPointer>{track})
        return 27;
    if (!announced.contains(retryId) || track->getId() != retryId || !cue->getId().isValid() || cue->isDirty() || track->isDirty())
        return 10;
    if (!q.exec("SELECT count(*) FROM library") || !q.next() || q.value(0).toInt() != 1)
        return 11;
    announced.clear();
    auto makeTrack = [&](const QString& name) {
        const auto path = temp.filePath(name);
        if (!QFile::copy(media.fileName(), path))
            return TrackPointer();
        auto result = Track::newTemporary(mixxx::FileAccess(mixxx::FileInfo(path)));
        result->createAndAddCue(mixxx::CueType::HotCue, 0, mixxx::audio::FramePos(100), mixxx::audio::kInvalidFramePos);
        return result;
    };
    auto first = makeTrack("batch-first.wav"), second = makeTrack("batch-second.wav");
    if (!first || !second)
        return 16;
    tracks.addTracksPrepare();
    if (!tracks.addTracksAddTrack(first, true).isValid())
        return 17;
    if (!q.exec("CREATE TEMP TRIGGER reject_cue_insert BEFORE INSERT ON cues BEGIN SELECT RAISE(FAIL, 'injected cue insert failure'); END"))
        return 18;
    if (tracks.addTracksAddTrack(second, true).isValid())
        return 19;
    if (tracks.addTracksFinish(false, &committedTracks) || first->getId().isValid() || second->getId().isValid() || !announced.isEmpty())
        return 20;
    if (!committedTracks.isEmpty())
        return 28;
    if (first->getCuePoints().first()->getId().isValid() || second->getCuePoints().first()->getId().isValid())
        return 21;
    if (!q.exec("SELECT count(*) FROM library") || !q.next() || q.value(0).toInt() != 1)
        return 22;
    // The trigger was created in the rolled-back transaction and is gone.
    tracks.addTracksPrepare();
    if (!tracks.addTracksAddTrack(first, true).isValid())
        return 23;
    if (tracks.addTracksFinish(true, &committedTracks) || first->getId().isValid() || !announced.isEmpty())
        return 24;
    if (!committedTracks.isEmpty())
        return 29;
    if (!q.exec("SELECT count(*) FROM library") || !q.next() || q.value(0).toInt() != 1)
        return 25;
    std::cout << "PASS: committed-track output is populated only on success and cleared on rejected commit, cue failure and explicit rollback.\n";
    std::cout << "PASS: a second-track cue failure rolls back both new tracks; explicit rollback also clears provisional IDs without announcements.\n";
    std::cout << "PASS: rejected batch leaves SQL empty, no announcement, invalid live IDs and dirty state; retry commits one track and accepts cue state. Registered cache ID removed on rollback, canonical-path lookup retains the same Track, retry registers its committed ID.\n";
}
