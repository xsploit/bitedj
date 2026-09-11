#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "library/dao/cuedao.h"
static void check(bool ok, const char* msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        check(dir.isValid(), "Temporary database unavailable");
        auto db = QSqlDatabase::addDatabase("QSQLITE", "cue-origin-probe");
        db.setDatabaseName(dir.filePath("probe.sqlite"));
        check(db.open(), "Database open failed");
        QSqlQuery q(db);
        check(q.exec("CREATE TABLE cues(id INTEGER PRIMARY KEY AUTOINCREMENT,track_id INTEGER,type INTEGER,position REAL,length REAL,hotcue INTEGER,label TEXT NOT NULL,color INTEGER,engine_library_uuid TEXT,engine_track_id TEXT,engine_bank INTEGER,engine_slot INTEGER)"), "Schema setup failed");
        CueDAO dao;
        dao.initialize(db);
        const TrackId id(QVariant(1));
        using F = mixxx::audio::FramePos;
        auto hot = CuePointer(new Cue(mixxx::CueType::HotCue, 0, F(132300.5), F(), mixxx::RgbColor(0xff8040)));
        auto loop = CuePointer(new Cue(mixxx::CueType::Loop, 26, F(441000.25), F(529200.75), mixxx::RgbColor(0x0c2238)));
        auto plain = CuePointer(new Cue(mixxx::CueType::HotCue, 5, F(100), F(), mixxx::RgbColor(0x102030)));
        Cue::EngineOrigin h{QStringLiteral("test-library"), QStringLiteral("1"), Cue::EngineOrigin::Bank::HotCue, 1};
        Cue::EngineOrigin l{QStringLiteral("test-library"), QStringLiteral("1"), Cue::EngineOrigin::Bank::SavedLoop, 1};
        hot->setEngineOrigin(h);
        loop->setEngineOrigin(l);
        dao.saveTrackCues(id, {hot, loop, plain});
        auto loaded = dao.getCuesForTrack(id);
        check(loaded.size() == 3, "Same-slot bank cue lost on reload");
        CuePointer lh, ll, lp;
        for (auto c : loaded) {
            if (c->getHotCue() == 0)
                lh = c;
            else if (c->getHotCue() == 26)
                ll = c;
            else
                lp = c;
        }
        check(lh && ll && lp, "Missing local cue indices");
        check(lh->getEngineOrigin() == h && ll->getEngineOrigin() == l, "Origin lost");
        check(!lp->getEngineOrigin(), "Plain cue acquired origin");
        check(!lh->isDirty(), "Loaded cue dirty");
        lh->setEngineOrigin(h);
        check(!lh->isDirty(), "Unchanged origin dirtied cue");
        check(lh->getPosition() == F(132300.5) && ll->getPosition() == F(441000.25) && ll->getEndPosition() == F(529200.75), "Fractional position lost");
        lh->setLabel("Edited label");
        ll->setColor(mixxx::RgbColor(0x010203));
        dao.saveTrackCues(id, loaded);
        loaded = dao.getCuesForTrack(id);
        for (auto c : loaded) {
            if (c->getHotCue() == 0)
                check(c->getEngineOrigin() == h && c->getLabel() == "Edited label", "Label edit lost origin");
            if (c->getHotCue() == 26)
                check(c->getEngineOrigin() == l && c->getColor() == mixxx::RgbColor(0x010203), "Color edit lost origin");
        }
        check(dao.deleteCuesForTrack(id), "Delete failed");
        check(dao.getCuesForTrack(id).empty(), "Deleted cues survived");
        check(q.exec("SELECT COUNT(*) FROM cues WHERE engine_library_uuid IS NOT NULL") && q.next() && q.value(0).toInt() == 0, "Deleted origin survived");
        std::cout << "PASS actual CueDAO: separate banks, fractional frames, origin persistence, edits, no-op setter, plain cues, deletion.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
