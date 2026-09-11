"""Exercise production Rekordbox table creation and SQLite query plans with Qt."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'src/library/rekordbox/rekordboxfeature.cpp').read_text()


def function(name):
    start = text.index('bool ' + name + '(')
    end = text.index('\n}\n', start) + 2
    return text[start:end]


source = r'''
#include <QtCore>
#include <QtSql>
#include <cstdlib>
#define LOG_FAILED_QUERY(q) qWarning() << (q).lastError()
void require(bool ok) { if (!ok) std::abort(); }
'''
source += '\n'.join(function(n) for n in (
    'createLibraryTable', 'createPlaylistsTable', 'createPlaylistTracksTable'))
source += r'''
void exec(QSqlDatabase& db, const QString& sql) {
    QSqlQuery q(db); if (!q.exec(sql)) { qWarning() << q.lastError(); std::abort(); }
}
QString plan(QSqlDatabase& db, const QString& sql) {
    QSqlQuery q(db); require(q.exec("EXPLAIN QUERY PLAN " + sql));
    QString result; while (q.next()) result += q.value(3).toString() + '\n';
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(":memory:");
    require(db.open());
    // An existing unindexed table must also acquire the index on initialization.
    require(createLibraryTable(db, "rekordbox_library"));
    exec(db, "DROP INDEX rekordbox_library_rb_device");
    for (int repeat = 0; repeat < 2; ++repeat) {
        require(createLibraryTable(db, "rekordbox_library"));
        require(createPlaylistsTable(db, "rekordbox_playlists"));
        require(createPlaylistTracksTable(db, "rekordbox_playlist_tracks"));
    }
    require(db.transaction());
    QSqlQuery insert(db);
    require(insert.prepare("INSERT INTO rekordbox_library "
                          "(rb_id, device, location, analyze_path) VALUES (?, ?, ?, ?)"));
    for (int i = 0; i < 20000; ++i) {
        insert.bindValue(0, i % 10000); insert.bindValue(1, i / 10000 ? "usb2" : "usb1");
        insert.bindValue(2, QString("/music/%1.wav").arg(i));
        // Shared analysis must remain legal; exported IDs may repeat on other devices.
        insert.bindValue(3, "/PIONEER/USBANLZ/shared.DAT"); require(insert.exec());
    }
    exec(db, "INSERT INTO rekordbox_playlists VALUES (1, 'Set')");
    exec(db, "INSERT INTO rekordbox_playlist_tracks (playlist_id, track_id, position) "
             "VALUES (1, 1, 2), (1, 2, 0), (1, 1, 1)");
    require(db.commit());
    const auto track = plan(db, "SELECT id FROM rekordbox_library WHERE rb_id=1234 AND device='usb2'");
    require(track.contains("SEARCH") && track.contains("_rb_device"));
    const auto playlist = plan(db, "SELECT track_id FROM rekordbox_playlist_tracks "
                                   "WHERE playlist_id=1 ORDER BY position");
    require(playlist.contains("_playlist") && !playlist.contains("TEMP B-TREE"));
    QSqlQuery q(db);
    require(q.exec("SELECT track_id FROM rekordbox_playlist_tracks WHERE playlist_id=1 ORDER BY position"));
    for (int expected : {2, 1, 1}) { require(q.next()); require(q.value(0).toInt() == expected); }
    require(!q.next());
    // A rejected duplicate location must not destroy the original rows.
    require(!q.exec("INSERT INTO rekordbox_library (id, rb_id, device, location, analyze_path) VALUES (30000, 1, 'usb1', '/music/0.wav', 'other')"));
    require(q.exec("SELECT COUNT(*) FROM rekordbox_library")); require(q.next()); require(q.value(0).toInt() == 20000);
    qInfo() << "PASS: indexed lookup, ordered duplicates, shared analysis and repeat initialization";
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'indexes.cpp'
    binary = Path(directory) / 'indexes'
    cpp.write_text(source)
    flags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', '--libs', 'Qt6Core', 'Qt6Sql'], text=True))
    subprocess.run(['c++', '-std=c++20', '-fPIC', str(cpp), '-o', str(binary), *flags], check=True)
    subprocess.run([str(binary)], check=True)

print("PASS: production Qt/SQLite library index tests (20,000 tracks)")
