#include <sqlite3.h>

#include <QApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <iostream>

#include "database/schemamanager.h"
#include "library/scanner/scannerglobal.h"
// Diagnostic-only access to the existing batch methods; no production header edits.
#define private public
#include "library/dao/trackdao.h"
#include "library/scanner/libraryscanner.h"
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
    QApplication app(argc, argv);
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
    LibraryScanner scanner({}, config);
    scanner.m_cueDao.initialize(db);
    scanner.m_playlistDao.initialize(db);
    scanner.m_analysisDao.initialize(db);
    scanner.m_libraryHashDao.initialize(db);
    scanner.m_trackDao.initialize(db);
    QList<TrackPointer> announced;
    QObject::connect(&scanner, &LibraryScanner::trackAdded, [&](TrackPointer track) { announced.append(track); });
    auto makeTrack = [&](int n) {auto path=temp.filePath(QString("track-%1.wav").arg(n));QFile::copy(media.fileName(),path);auto track=Track::newTemporary(mixxx::FileAccess(mixxx::FileInfo(path)));track->createAndAddCue(mixxx::CueType::HotCue,0,mixxx::audio::FramePos(100),mixxx::audio::kInvalidFramePos);return track; };
    auto handle = db.driver()->handle();
    if (!handle.isValid() || QByteArray(handle.typeName()) != "sqlite3*")
        return 30;
    auto* sqlite = *static_cast<sqlite3**>(handle.data());
    if (!sqlite)
        return 31;
    for (int n = 0; n < 2; ++n) {
        scanner.m_scannerGlobal = ScannerGlobalPointer(new ScannerGlobal({}, {}, {}, {}, {}));
        // Canceled scans intentionally commit accepted work while skipping cleanup.
        scanner.m_scannerGlobal->cancel();
        auto track = makeTrack(n);
        scanner.m_trackDao.addTracksPrepare();
        if (!scanner.m_trackDao.addTracksAddTrack(track, true).isValid())
            return 6;
        if (!announced.isEmpty())
            return 7;
        if (n == 0)
            sqlite3_commit_hook(sqlite, [](void*) { return 1; }, nullptr);
        scanner.slotFinishUnhashedScan();
        sqlite3_commit_hook(sqlite, nullptr, nullptr);
        if (n == 0 && (!announced.isEmpty() || track->getId().isValid()))
            return 8;
        if (n == 1 && (announced != QList<TrackPointer>{track} || !track->getId().isValid() || track->isDirty()))
            return 9;
    }
    std::cout << "PASS real LibraryScanner finish slot: rejected commit emits no trackAdded; canceled-scan successful commit emits exactly the committed Track. Synthetic direct DAO staging, scanner worker and discovery not started.\n";
}
