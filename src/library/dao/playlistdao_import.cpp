#include "library/dao/playlistdao.h"
#include "util/db/sqltransaction.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <limits>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
}

bool PlaylistDAO::stageNewPlaylist(const SqlTransaction& transaction,
        const QString& name, const QList<TrackId>& tracks,
        StagedNewPlaylist* result, QString* error) {
    if (error) error->clear();
    if (!result) return fail(error, "Missing staged playlist output");
    // Capture inputs before clearing output, including aliases into it.
    const QString desiredName = name;
    const QList<TrackId> desiredTracks = tracks;
    *result = {};
    if (!transaction || transaction.database().connectionName() != m_database.connectionName() ||
            QThread::currentThread() != thread())
        return fail(error, "Playlist staging requires an active transaction on the DAO thread and connection");
    if (desiredName.trimmed().isEmpty() || desiredName.contains(QChar::Null) || desiredTracks.size() > 1000000)
        return fail(error, "Invalid imported playlist name or size");
    QSqlQuery query(m_database);
    if (!query.exec("SAVEPOINT engine_new_playlist")) return fail(error, query.lastError().text());
    const auto abort = [&](const QString& message) {
        QSqlQuery rollback(m_database);
        rollback.exec("ROLLBACK TO engine_new_playlist");
        rollback.exec("RELEASE engine_new_playlist");
        return fail(error, message);
    };
    if (!query.prepare("SELECT 1 FROM library WHERE id=?")) return abort(query.lastError().text());
    QSet<TrackId> checked;
    for (const auto& id : desiredTracks) {
        if (!id.isValid()) return abort("Invalid track identity");
        if (checked.contains(id)) continue;
        query.bindValue(0, id.toVariant());
        if (!query.exec()) return abort(query.lastError().text());
        if (!query.next()) return abort("Playlist references a missing track");
        query.finish();
        checked.insert(id);
    }
    if (!query.exec("SELECT COALESCE(MAX(position),0) FROM Playlists") || !query.next())
        return abort(query.lastError().text());
    const qint64 position = query.value(0).toLongLong();
    if (position >= std::numeric_limits<int>::max()) return abort("Playlist position overflow");
    query.finish();
    if (!query.prepare("INSERT INTO Playlists(name,position,hidden,date_created,date_modified) "
                       "VALUES(?,?,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)")) return abort(query.lastError().text());
    query.addBindValue(desiredName);
    query.addBindValue(position + 1);
    if (!query.exec()) return abort(query.lastError().text());
    const qint64 id = query.lastInsertId().toLongLong();
    if (id <= 0 || id > std::numeric_limits<int>::max()) return abort("Playlist identity overflow");
    StagedNewPlaylist staged{static_cast<int>(id), desiredName, desiredTracks, {}};
    if (!query.prepare("INSERT INTO PlaylistTracks(playlist_id,track_id,position,pl_datetime_added) "
                       "VALUES(?,?,?,CURRENT_TIMESTAMP)")) return abort(query.lastError().text());
    int index = 1;
    for (const auto& track : desiredTracks) {
        query.bindValue(0, staged.id);
        query.bindValue(1, track.toVariant());
        query.bindValue(2, index++);
        if (!query.exec()) return abort(query.lastError().text());
        staged.entryIds.append(query.lastInsertId().toLongLong());
    }
    if (!query.exec("RELEASE engine_new_playlist")) return abort(query.lastError().text());
    *result = std::move(staged);
    return true;
}

bool PlaylistDAO::commitNewPlaylists(SqlTransaction& transaction,
        const QList<StagedNewPlaylist>& playlists, QString* error) {
    if (error) error->clear();
    if (!transaction || transaction.database().connectionName() != m_database.connectionName() ||
            QThread::currentThread() != thread())
        return fail(error, "Playlist publication requires the active staging transaction and DAO thread");
    QSet<int> ids;
    QSqlQuery query(m_database);
    for (const auto& playlist : playlists) {
        if (playlist.id <= 0 || ids.contains(playlist.id) || playlist.tracks.size() != playlist.entryIds.size())
            return fail(error, "Invalid or duplicate staged playlist");
        ids.insert(playlist.id);
        query.prepare("SELECT name,hidden FROM Playlists WHERE id=?");
        query.addBindValue(playlist.id);
        if (!query.exec() || !query.next() || query.value(0).toString() != playlist.name || query.value(1).toInt() != 0)
            return fail(error, "Staged playlist changed before commit");
        query.finish();
        query.prepare("SELECT id,track_id,position FROM PlaylistTracks WHERE playlist_id=? ORDER BY position,id");
        query.addBindValue(playlist.id);
        if (!query.exec()) return fail(error, query.lastError().text());
        qsizetype index = 0;
        while (query.next()) {
            if (index >= playlist.tracks.size() || query.value(0).toLongLong() != playlist.entryIds[index] ||
                    TrackId(query.value(1)) != playlist.tracks[index] || query.value(2).toLongLong() != index + 1)
                return fail(error, "Staged playlist entries changed before commit");
            ++index;
        }
        if (query.lastError().isValid() || index != playlist.tracks.size())
            return fail(error, "Incomplete staged playlist entries");
        query.finish();
    }
    if (!transaction.commit()) return fail(error, "Import transaction commit failed");
    // All caches are populated before the first observer can react to a signal.
    for (const auto& playlist : playlists)
        for (const auto& track : playlist.tracks)
            m_playlistsTrackIsIn.insert(track, playlist.id);
    for (const auto& playlist : playlists) {
        emit added(playlist.id);
        int position = 1;
        for (const auto& track : playlist.tracks) emit trackAdded(playlist.id, track, position++);
    }
    if (!ids.isEmpty()) {
        emit tracksAdded(ids);
        emit playlistContentChanged(ids);
    }
    return true;
}
