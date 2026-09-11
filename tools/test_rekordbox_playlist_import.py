#!/usr/bin/env python3
"""Run production playlist import functions against Qt/SQLite fixtures.

Needs a C++17 compiler and Qt6Core/Qt6Sql pkg-config packages. This deliberately
compiles the functions extracted from rekordboxfeature.cpp, not a duplicate
implementation. TreeItem is a minimal test double; the complete UI/PDB parser
is outside this standalone test's scope. The regular application build remains
a separate check.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"\n(?:int|void) " + name + r"\([^;]*?\) \{", source)
    if not match:
        raise RuntimeError("Cannot locate production function " + name)
    end = source.index("\n}\n", match.end()) + 2
    return source[match.start():end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path,
                    default=Path(__file__).resolve().parents[1] /
                    "src/library/rekordbox/rekordboxfeature.cpp")
args = parser.parse_args()
source = args.source.read_text()
cpp = r'''
#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <memory>
#include <vector>
#include <limits>
#include <cstdlib>
#include <cstdio>
static const QString kRekordboxLibraryTable = "rekordbox_library";
static const QString kRekordboxPlaylistsTable = "rekordbox_playlists";
static const QString kRekordboxPlaylistTracksTable = "rekordbox_playlist_tracks";
static const QString kPLaylistPathDelimiter = "-->";
static const QString IS_NOT_RECORDBOX_DEVICE = "0";
static const int kInvalidPlaylistId = -1;
#define LOG_FAILED_QUERY(query) qWarning() << (query).lastError()
struct TreeItem {
    QString label;
    std::vector<std::unique_ptr<TreeItem>> children;
    TreeItem* appendChild(const QString& name, const QVariant&) {
        auto child = std::make_unique<TreeItem>();
        child->label = name;
        children.push_back(std::move(child));
        return children.back().get();
    }
};
void require(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
'''
cpp += function(source, "findTrackId") + "\n" + function(source, "buildPlaylistTree")
cpp += r'''
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(":memory:");
    require(db.open(), "open database");
    QSqlQuery sql(db);
    for (const char* statement : {
            "PRAGMA foreign_keys=ON",
            "CREATE TABLE rekordbox_library(id INTEGER PRIMARY KEY, rb_id INTEGER, device TEXT)",
            "CREATE TABLE rekordbox_playlists(id INTEGER PRIMARY KEY, name TEXT UNIQUE)",
            "CREATE TABLE rekordbox_playlist_tracks(playlist_id INTEGER REFERENCES rekordbox_playlists(id), track_id INTEGER REFERENCES rekordbox_library(id), position INTEGER)",
            "INSERT INTO rekordbox_library VALUES(101,1,'A'),(102,2,'A'),(201,1,'B')"}) {
        require(sql.exec(statement), "schema/fixture setup");
    }
    QMap<uint32_t, QString> names{{10,"Folder"},{20,"Empty"},{30,"Set"}};
    QMap<uint32_t, bool> folders{{10,true},{20,false},{30,false}};
    QMap<uint32_t,QMap<uint32_t,uint32_t>> tree{
            {0,{{2,10},{7,20}}}, {10,{{4,30}}}};
    QMap<uint32_t,QMap<uint32_t,uint32_t>> tracks{{30,{{1,2},{5,1},{9,99}}}};
    const auto originalTree = tree;
    const auto originalTracks = tracks;
    const auto originalNames = names;
    const auto originalFolders = folders;
    TreeItem root;
    buildPlaylistTree(db,&root,0,names,folders,tree,tracks,"/usb/A","A");
    require(tree==originalTree && tracks==originalTracks && names==originalNames &&
            folders==originalFolders, "import must not create records for gaps in sort keys");
    require(root.children.size()==2 && root.children[0]->label=="Folder" &&
            root.children[1]->label=="Empty", "sparse sibling order/empty playlist");
    require(root.children[0]->children.size()==1 &&
            root.children[0]->children[0]->label=="Set", "nested playlist");
    require(sql.exec("SELECT t.track_id,t.position FROM rekordbox_playlist_tracks t JOIN rekordbox_playlists p ON p.id=t.playlist_id WHERE p.name='/usb/A-->Folder-->Set' ORDER BY t.position"), "playlist read");
    require(sql.next() && sql.value(0).toInt()==102 && sql.value(1).toInt()==1,
            "first exported entry");
    require(sql.next() && sql.value(0).toInt()==101 && sql.value(1).toInt()==5,
            "second exported entry is from device A");
    require(!sql.next(), "dangling track reference is skipped");
    TreeItem refreshed;
    buildPlaylistTree(db,&refreshed,0,names,folders,tree,tracks,"/usb/A","A");
    require(sql.exec("SELECT count(*) FROM rekordbox_playlist_tracks") && sql.next() &&
            sql.value(0).toInt()==2, "reimport must not duplicate links");
    // Sort keys are unsigned exported positions, not a count or array offset.
    tree={{0,{{std::numeric_limits<uint32_t>::max(),30}}}};
    tracks={{30,{{0,1},{std::numeric_limits<uint32_t>::max(),2}}}};
    TreeItem extremes;
    buildPlaylistTree(db,&extremes,0,names,folders,tree,tracks,"/usb/extremes","A");
    require(extremes.children.size()==1, "large sort key does not imply billions of children");
    require(sql.exec("SELECT t.track_id,t.position FROM rekordbox_playlist_tracks t JOIN rekordbox_playlists p ON p.id=t.playlist_id WHERE p.name='/usb/extremes-->Set' ORDER BY t.position"), "extreme positions read");
    require(sql.next() && sql.value(0).toInt()==101 && sql.value(1).toLongLong()==0,
            "zero position terminates and retains its entry");
    require(sql.next() && sql.value(0).toInt()==102 && sql.value(1).toLongLong()==4294967295LL,
            "unsigned position remains positive in SQLite");
    require(!sql.next(), "exactly two extreme-position entries");
    TreeItem absent;
    buildPlaylistTree(db,&absent,123,names,folders,tree,tracks,"/usb/absent","A");
    require(absent.children.empty() && !tree.contains(123), "absent parent is not inserted");
    qInfo() << "PASS: sparse/nested playlists, device identity, missing tracks, reimport, zero/large positions, immutable inputs";
}
'''
flags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "--libs", "Qt6Core", "Qt6Sql"], text=True))
with tempfile.TemporaryDirectory(prefix="bitedj-playlist-test-") as temp:
    path = Path(temp)
    (path / "test.cpp").write_text(cpp)
    subprocess.run(["c++", "-std=c++17", "-fPIC", str(path / "test.cpp"),
                    "-o", str(path / "test"), *flags], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)

print("PASS: production playlist importer passed all Qt/SQLite fixtures")
