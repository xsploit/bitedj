#include <gtest/gtest.h>

#include <QTest>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QSqlQuery>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

#include "library/engine/engineimportcoordinator.h"
#include "util/fpclassify.h"

#include "control/controlindicatortimer.h"
#include "database/mixxxdb.h"
#include "effects/backends/builtin/graphiceqeffect.h"
#include "effects/backends/effectmanifest.h"
#include "effects/effectchain.h"
#include "effects/effectslot.h"
#include "effects/effectsmanager.h"
#include "engine/channels/enginedeck.h"
#include "engine/enginebuffer.h"
#include "engine/enginemixer.h"
#include "library/coverartcache.h"
#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "library/trackcollection.h"
#include "mixer/basetrackplayer.h"
#include "mixer/deck.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxdbtest.h"
#include "test/soundsourceproviderregistration.h"
#include "track/track.h"
#include "util/cmdlineargs.h"

namespace {

const QString kTrackLocationTest1 = QStringLiteral("id3-test-data/cover-test-png.mp3");
const QString kTrackLocationTest2 = QStringLiteral("id3-test-data/cover-test-vbr.mp3");

void deleteTrack(Track* pTrack) {
    // Delete track objects directly in unit tests with
    // no main event loop
    delete pTrack;
};

void waitForTrackToBeLoaded(Deck* pDeck) {
    while (!pDeck->getEngineDeck()->getEngineBuffer()->isTrackLoaded()) {
        QTest::qSleep(100); // millis
    }
}

} // namespace

// We can't inherit from LibraryTest because that creates a key_notation control object that is also
// created by the Library object itself. The duplicated CO creation causes a debug assert.
class PlayerManagerTest : public MixxxDbTest, SoundSourceProviderRegistration {
  public:
    PlayerManagerTest()
            : MixxxDbTest(true) {
    }

    void SetUp() override {
        // This setup mirrors coreservices -- it would be nice if we could use coreservices instead
        // but it does a lot of local disk / settings setup.
        auto pChannelHandleFactory = std::make_shared<ChannelHandleFactory>();
        m_pEffectsManager = std::make_shared<EffectsManager>(m_pConfig, pChannelHandleFactory);
        m_pEngine = std::make_shared<EngineMixer>(
                m_pConfig,
                "[Master]",
                m_pEffectsManager.get(),
                pChannelHandleFactory,
                true);
        m_pSoundManager = std::make_shared<SoundManager>(m_pConfig, m_pEngine.get());
        m_pControlIndicatorTimer = std::make_shared<mixxx::ControlIndicatorTimer>(nullptr);
        m_pEngine->registerNonEngineChannelSoundIO(m_pSoundManager.get());

        CoverArtCache::createInstance();

        m_pPlayerManager = std::make_shared<PlayerManager>(m_pConfig,
                m_pSoundManager.get(),
                m_pEffectsManager.get(),
                m_pEngine.get());

        m_pPlayerManager->addConfiguredDecks();
        m_pPlayerManager->addSampler();
        // Library's EDMC feature binds preview controls, just as it does in
        // CoreServices. Construct the real preview deck before the library.
        m_pPlayerManager->addPreviewDeck();
        PlayerInfo::create();
        m_pEffectsManager->setup();

        const auto dbConnection = mixxx::DbConnectionPooled(dbConnectionPooler());
        if (!MixxxDb::initDatabaseSchema(dbConnection)) {
            exit(1);
        }
        m_pTrackCollectionManager = std::make_unique<TrackCollectionManager>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                deleteTrack);

        m_pRecordingManager = std::make_shared<RecordingManager>(m_pConfig, m_pEngine.get());
        m_pLibrary = std::make_shared<Library>(
                nullptr,
                m_pConfig,
                dbConnectionPooler(),
                m_pTrackCollectionManager.get(),
                m_pPlayerManager.get(),
                m_pRecordingManager.get());

        m_pPlayerManager->bindToLibrary(m_pLibrary.get());
    }

    ~PlayerManagerTest() {
        m_pSoundManager.reset();
        m_pPlayerManager.reset();
        PlayerInfo::destroy();
        m_pLibrary.reset();
        m_pRecordingManager.reset();
        m_pEngine.reset();
        m_pEffectsManager.reset();
        m_pTrackCollectionManager.reset();
        m_pControlIndicatorTimer.reset();
        CoverArtCache::destroy();
    }

  protected:
    TrackPointer getOrAddTrackByLocation(
            const QString& trackLocation) const {
        return m_pTrackCollectionManager->getOrAddTrack(
                TrackRef::fromFilePath(trackLocation));
    }

    std::shared_ptr<EffectsManager> m_pEffectsManager;
    std::shared_ptr<mixxx::ControlIndicatorTimer> m_pControlIndicatorTimer;
    std::shared_ptr<EngineMixer> m_pEngine;
    std::shared_ptr<SoundManager> m_pSoundManager;
    std::shared_ptr<PlayerManager> m_pPlayerManager;
    std::unique_ptr<TrackCollectionManager> m_pTrackCollectionManager;
    std::shared_ptr<RecordingManager> m_pRecordingManager;
    std::shared_ptr<Library> m_pLibrary;
};

// Bite DJ: the skin's Settings -> EQ page binds to the output chain's first
// effect slot assuming the 8-band Graphic EQ; EffectsManager::setup() must
// guarantee it is loaded there.
TEST_F(PlayerManagerTest, MainOutputHasGraphicEq) {
    auto pChain = m_pEffectsManager->getOutputEffectChain();
    ASSERT_NE(nullptr, pChain);
    auto pSlot = pChain->getEffectSlot(0);
    ASSERT_NE(nullptr, pSlot);
    const EffectManifestPointer pManifest = pSlot->getManifest();
    ASSERT_NE(nullptr, pManifest);
    EXPECT_EQ(GraphicEQEffect::getId(), pManifest->id());
}

TEST_F(PlayerManagerTest, UnEjectTest) {
    // Ejecting an empty deck with no previously-recorded ejected track has no effect.
    auto deck1 = m_pPlayerManager->getDeck(0);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Load a track and eject it
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1, false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    // make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2, false);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay); // millis
    deck2->slotEjectTrack(2.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

// Loading a new track in a deck causes the old one to be ejected.
// That old track can be unejected into a different deck.
TEST_F(PlayerManagerTest, UnEjectReplaceTrackTest) {
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track and the load another one
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1, false);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Load another track, replacing the first, causing it to be unloaded.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2, false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);

    // Ejecting in an empty deck loads the last-ejected track.
    auto deck2 = m_pPlayerManager->getDeck(1);
    ASSERT_EQ(nullptr, deck2->getLoadedTrack());
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck2->slotEjectTrack(1.0);
    ASSERT_NE(nullptr, deck2->getLoadedTrack());
    ASSERT_EQ(testId1, deck2->getLoadedTrack()->getId());
}

TEST_F(PlayerManagerTest, UnEjectInvalidTrackIdTest) {
    // Save an invalid trackid in playermanager.
    auto pTrack = Track::newDummy(
            getTestDir().filePath(kTrackLocationTest1), TrackId(QVariant(10)));
    ASSERT_NE(nullptr, pTrack);
    m_pPlayerManager->slotSaveEjectedTrack(pTrack);
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Does nothing -- no crash.
    // make sure eject does not trigger 'unreplace'
    QTest::qSleep(kUnreplaceDelay);
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());
}

TEST_F(PlayerManagerTest, UnReplaceTest) {
    // Trigger eject twice within 500 ms to undo track replacement
    auto deck1 = m_pPlayerManager->getDeck(0);
    // Load a track
    TrackPointer pTrack1 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest1));
    ASSERT_NE(nullptr, pTrack1);
    TrackId testId1 = pTrack1->getId();
    ASSERT_TRUE(testId1.isValid());
    deck1->slotLoadTrack(pTrack1, false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Load another track.
    TrackPointer pTrack2 = getOrAddTrackByLocation(getTestDir().filePath(kTrackLocationTest2));
    ASSERT_NE(nullptr, pTrack2);
    deck1->slotLoadTrack(pTrack2, false);
    m_pEngine->process(1024);
    waitForTrackToBeLoaded(deck1);
    ASSERT_NE(nullptr, deck1->getLoadedTrack());

    // Eject. Make sure eject does not trigger 'unreplace':
    // sleep for longer than 500 ms 'unreplace' period so this is not registered as double-click
    QTest::qSleep(kUnreplaceDelay); // millis
    deck1->slotEjectTrack(1.0);
    ASSERT_EQ(nullptr, deck1->getLoadedTrack());

    // Eject again, assume this is reached faster than 500 ms after first eject
    deck1->slotEjectTrack(1.0);
    // First track should be reloaded
    ASSERT_NE(nullptr, deck1->getLoadedTrack());
    ASSERT_EQ(testId1, deck1->getLoadedTrack()->getId());
}

// Run the real coordinator while a separately paced thread processes two actual
// decoded deck buffers. This does not open a sound device or establish Pi timing.
TEST_F(PlayerManagerTest, EngineImportWhileTwoDecksProcessAudio) {
#ifndef __SQLITE3__
    GTEST_SKIP() << "Engine Apply requires native SQLite";
#else
    QTemporaryDir media;
    ASSERT_TRUE(media.isValid());
    const auto libraryPath = media.filePath("Engine Library");
    ASSERT_TRUE(QDir().mkpath(libraryPath));
    const auto seed = media.filePath("track-1.wav");
    ASSERT_TRUE(QFile::copy(getTestDir().filePath("sine-30.wav"), seed));
    constexpr int kTracks = 500;
    QJsonArray sources;
    for (int i = 1; i <= kTracks; ++i) {
        const auto name = QString("track-%1.wav").arg(i);
        if (i > 1) {
            std::error_code error;
            std::filesystem::create_hard_link(seed.toStdString(), media.filePath(name).toStdString(), error);
            ASSERT_FALSE(error) << error.message();
        }
        sources.append(QJsonObject{{"id", QString::number(i)}, {"title", name},
                {"relativePath", QString("../" + name)}, {"artist", QJsonValue::Null},
                {"album", QJsonValue::Null}, {"genre", QJsonValue::Null},
                {"bpm", QJsonValue::Null}, {"durationMs", QJsonValue::Null},
                {"mainCueFrame", QJsonValue::Null}, {"sampleCount", "0"},
                {"sampleRate", 44100}, {"hotCues", QJsonArray{}}, {"loops", QJsonArray{}},
                {"sameSlotCollisions", QJsonArray{}}, {"beatgrid", QJsonArray{}}});
    }
    QList<TrackPointer> playing;
    for (int i = 0; i < 2; ++i) {
        const auto track = getOrAddTrackByLocation(media.filePath(QString("track-%1.wav").arg(i+1)));
        ASSERT_TRUE(track);
        playing.append(track);
        auto* deck = m_pPlayerManager->getDeck(i);
        deck->slotLoadTrack(track, true);
        m_pEngine->process(1024);
        QElapsedTimer loading; loading.start();
        while (!deck->getEngineDeck()->getEngineBuffer()->isTrackLoaded() && loading.elapsed() < 3000)
            QTest::qWait(5);
        ASSERT_TRUE(deck->getEngineDeck()->getEngineBuffer()->isTrackLoaded());
        ControlObject::set(ConfigKey(PlayerManager::groupForDeck(i), "main_mix"), 1);
        ControlObject::set(ConfigKey(PlayerManager::groupForDeck(i), "volume"), 1);
        ControlObject::set(ConfigKey(PlayerManager::groupForDeck(i), "play"), 1);
    }
    std::atomic<int> callbacks{0}, audible1{0}, audible2{0}, nonfinite{0};
    std::atomic<long long> maxCallbackUs{0};
    std::jthread audio([&](std::stop_token stop) {
        auto next = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            const auto start = std::chrono::steady_clock::now();
            m_pEngine->process(1024);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
            maxCallbackUs.store(std::max(maxCallbackUs.load(), static_cast<long long>(elapsed)));
            for (int deck = 0; deck < 2; ++deck) {
                const auto* pcm = m_pEngine->getChannelBuffer(PlayerManager::groupForDeck(deck));
                bool audible = false;
                for (int sample = 0; sample < 1024; ++sample) {
                    if (!util_isfinite(double(pcm[sample]))) ++nonfinite;
                    if (std::abs(pcm[sample]) > .001f) audible = true;
                }
                if (audible) ++(deck ? audible2 : audible1);
            }
            ++callbacks;
            next += std::chrono::microseconds(11610); // 512 stereo frames / 44.1 kHz
            std::this_thread::sleep_until(next);
        }
    });
    QTest::qWait(200);
    const int before = callbacks, beforeAudible1 = audible1, beforeAudible2 = audible2;
    mixxx::EngineImportCoordinator importer(m_pTrackCollectionManager.get());
    bool finished = false, cancelled = false;
    int imported = 0, attention = -1;
    QObject::connect(&importer, &mixxx::EngineImportCoordinator::finished,
            [&](int tracks, int, int notices, bool cancel, const QStringList&) {
                imported = tracks; attention = notices; cancelled = cancel; finished = true;
            });
    const QJsonObject package{{"protocol", "bitedj.engine.import"}, {"protocolVersion", 1},
            {"schema", "3.0.2"}, {"sourceUuid", "concurrent-audio-fixture"},
            {"frameUnit", "audio frames at track sample rate"},
            {"mediaPathContext", QJsonObject{{"libraryDirectory", libraryPath},
                    {"relativePathBase", "original Engine Library directory"}}},
            {"tracks", sources}, {"playlists", QJsonArray{}}};
    QString error;
    ASSERT_TRUE(importer.start(package, libraryPath, media.path(), &error)) << error.toStdString();
    QElapsedTimer wait; wait.start();
    while (!finished && wait.elapsed() < 15000) QTest::qWait(2);
    audio.request_stop(); audio.join();
    ASSERT_TRUE(finished);
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(kTracks, imported);
    EXPECT_EQ(0, attention);
    EXPECT_GT(callbacks.load() - before, 0);
    EXPECT_GT(audible1.load() - beforeAudible1, 0);
    EXPECT_GT(audible2.load() - beforeAudible2, 0);
    EXPECT_EQ(0, nonfinite.load());
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(playing[i], m_pPlayerManager->getDeck(i)->getLoadedTrack());
        EXPECT_EQ(1, ControlObject::get(ConfigKey(PlayerManager::groupForDeck(i), "play")));
    }
    const auto db = m_pTrackCollectionManager->internalCollection()->database();
    QSqlQuery count(db);
    ASSERT_TRUE(count.exec("SELECT count(*) FROM library") && count.next());
    EXPECT_EQ(kTracks, count.value(0).toInt());
    ASSERT_TRUE(count.exec("PRAGMA integrity_check") && count.next());
    EXPECT_EQ("ok", count.value(0).toString());
    std::cout << "CONCURRENT_IMPORT tracks=" << imported << " callbacksDuringImport=" << callbacks-before
              << " audibleDeck1=" << audible1-beforeAudible1 << " audibleDeck2=" << audible2-beforeAudible2
              << " maxCallbackUs=" << maxCallbackUs << " importMs=" << wait.elapsed() << '\n';
#endif
}
