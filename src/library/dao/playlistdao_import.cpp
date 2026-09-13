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
        StagedPlaylist* result, QString* error) {
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
    StagedPlaylist staged{static_cast<int>(id), desiredName, desiredTracks, {}, true, {}, {}};
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

bool PlaylistDAO::commitStagedPlaylists(SqlTransaction& transaction,
        const QList<StagedPlaylist>& playlists, QString* error) {
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
        query.prepare("SELECT name,hidden,locked FROM Playlists WHERE id=?");
        query.addBindValue(playlist.id);
        if (!query.exec() || !query.next() || query.value(0).toString() != playlist.name || query.value(1).toInt() != 0 || query.value(2).toInt() != 0)
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
    for (auto it = m_playlistsTrackIsIn.begin(); it != m_playlistsTrackIsIn.end();) {
        if (ids.contains(it.value())) it = m_playlistsTrackIsIn.erase(it);
        else ++it;
    }
    for (const auto& playlist : playlists)
        for (const auto& track : playlist.tracks)
            m_playlistsTrackIsIn.insert(track, playlist.id);
    for (const auto& playlist : playlists) {
        if (playlist.isNew) emit added(playlist.id);
        else {
            if (playlist.name != playlist.previousName) emit renamed(playlist.id, playlist.name);
            int oldPosition = 1;
            for (const auto& track : playlist.previousTracks) emit trackRemoved(playlist.id, track, oldPosition++);
        }
        int position = 1;
        for (const auto& track : playlist.tracks) emit trackAdded(playlist.id, track, position++);
    }
    if (!ids.isEmpty()) {
        emit tracksRemoved(ids);
        emit tracksAdded(ids);
        emit tracksMoved(ids);
        emit playlistContentChanged(ids);
    }
    return true;
}

bool PlaylistDAO::stagePlaylistUpdate(const SqlTransaction& transaction,
        const StagedPlaylist& expected, const QString& name,
        const QList<TrackId>& tracks, const QList<qint64>& entryIds,
        StagedPlaylist* result, QString* error) {
    if (error) error->clear();
    if (!result) return fail(error, "Missing staged playlist output");
    const StagedPlaylist before = expected;
    const QString desiredName = name;
    const QList<TrackId> desiredTracks = tracks;
    const QList<qint64> desiredIds = entryIds;
    *result = {};
    if (!transaction || transaction.database().connectionName() != m_database.connectionName() ||
            QThread::currentThread() != thread())
        return fail(error, "Playlist update requires the active DAO transaction and thread");
    if (before.id <= 0 || before.tracks.size() != before.entryIds.size() ||
            desiredTracks.size() != desiredIds.size() || desiredTracks.size() > 1000000 ||
            desiredName.trimmed().isEmpty() || desiredName.contains(QChar::Null))
        return fail(error, "Invalid playlist update");
    QSqlQuery query(m_database);
    query.prepare("SELECT name,hidden,locked FROM Playlists WHERE id=?");
    query.addBindValue(before.id);
    if (!query.exec() || !query.next() || query.value(0).toString() != before.name ||
            query.value(1).toInt() != 0 || query.value(2).toInt() != 0)
        return fail(error, "Playlist changed, hidden or locked");
    query.finish();
    query.prepare("SELECT id,track_id,position FROM PlaylistTracks WHERE playlist_id=? ORDER BY position,id");
    query.addBindValue(before.id);
    if (!query.exec()) return fail(error, query.lastError().text());
    qsizetype index = 0;
    QSet<qint64> originalIds;
    while (query.next()) {
        if (index >= before.entryIds.size() || query.value(0).toLongLong() != before.entryIds[index] ||
                TrackId(query.value(1)) != before.tracks[index] || query.value(2).toLongLong() != index+1)
            return fail(error, "Playlist entries changed since snapshot");
        originalIds.insert(before.entryIds[index++]);
    }
    if (query.lastError().isValid() || index != before.entryIds.size()) return fail(error, "Incomplete playlist snapshot");
    query.finish();
    QSet<qint64> retained;
    for (auto id : desiredIds) {
        if (id < 0 || (id && (!originalIds.contains(id) || retained.contains(id))))
            return fail(error, "Foreign or duplicate playlist occurrence identity");
        if (id) retained.insert(id);
    }
    query.prepare("SELECT 1 FROM library WHERE id=?");
    QSet<TrackId> checked;
    for (const auto& track : desiredTracks) {
        if (!track.isValid()) return fail(error, "Invalid track identity");
        if (checked.contains(track)) continue;
        query.bindValue(0, track.toVariant());
        if (!query.exec() || !query.next()) return fail(error, "Playlist references a missing track");
        query.finish(); checked.insert(track);
    }
    if (!query.exec("SAVEPOINT engine_update_playlist")) return fail(error, query.lastError().text());
    const auto abort = [&](const QString& message) {
        QSqlQuery rollback(m_database);
        rollback.exec("ROLLBACK TO engine_update_playlist");
        rollback.exec("RELEASE engine_update_playlist");
        return fail(error, message);
    };
    StagedPlaylist staged{before.id, desiredName, desiredTracks, {}, false, before.name, before.tracks};
    for (qsizetype i=0;i<desiredTracks.size();++i) {
        if (desiredIds[i]) {
            query.prepare("UPDATE PlaylistTracks SET track_id=?,position=? WHERE playlist_id=? AND id=?");
            query.addBindValue(desiredTracks[i].toVariant());query.addBindValue(i+1);
            query.addBindValue(before.id);query.addBindValue(desiredIds[i]);
        } else {
            query.prepare("INSERT INTO PlaylistTracks(track_id,position,playlist_id,pl_datetime_added) VALUES(?,?,?,CURRENT_TIMESTAMP)");
            query.addBindValue(desiredTracks[i].toVariant());query.addBindValue(i+1);query.addBindValue(before.id);
        }
        if (!query.exec() || query.numRowsAffected()!=1) return abort("Playlist occurrence write failed");
        staged.entryIds.append(desiredIds[i] ? desiredIds[i] : query.lastInsertId().toLongLong());
    }
    // Insert new occurrences before deleting old rows so SQLite cannot reuse
    // a removed highest rowid within this update.
    query.prepare("DELETE FROM PlaylistTracks WHERE playlist_id=? AND id=?");
    for (auto id : originalIds) {
        if (retained.contains(id)) continue;
        query.bindValue(0, before.id);query.bindValue(1,id);
        if (!query.exec()) return abort(query.lastError().text());
    }
    query.prepare("UPDATE Playlists SET name=?,date_modified=CURRENT_TIMESTAMP WHERE id=?");
    query.addBindValue(desiredName);query.addBindValue(before.id);
    if (!query.exec() || query.numRowsAffected()!=1) return abort("Playlist update failed");
    if (!query.exec("RELEASE engine_update_playlist")) return abort(query.lastError().text());
    *result=std::move(staged);
    return true;
}
