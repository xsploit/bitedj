#include "library/engine/engineimportcoordinator.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QThread>
#include <limits>

#include "library/engine/engineimportpackage.h"
#include "library/dao/fscueoverridestore.h"
#include "library/dao/fsmetaoverridestore.h"
#include "preferences/systemsettings.h"
#include "library/engine/engineimportregistry.h"
#include "library/engine/enginemediaresolver.h"
#include "library/engine/enginemetadataimport.h"
#include "library/engine/engineplaylistimport.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/db/sqltransaction.h"

namespace mixxx {
namespace {
bool sameRecord(const std::optional<EngineImportRecord>& a,
        const std::optional<EngineImportRecord>& b) {
    return a.has_value() == b.has_value() &&
            (!a || (a->localId == b->localId && a->baseline == b->baseline));
}
} // namespace
EngineImportCoordinator::EngineImportCoordinator(TrackCollectionManager* manager, QObject* parent)
        : QObject(parent), m_manager(manager) {
}
EngineImportCoordinator::~EngineImportCoordinator() = default;
bool EngineImportCoordinator::start(const QJsonObject& package,
        const QString& libraryDirectory, const QString& mediaRoot, QString* error) {
    if (m_running || !m_manager || QThread::currentThread() != m_manager->thread()) {
        if (error) *error = tr("The importer is unavailable or already running.");
        return false;
    }
#ifndef __SQLITE3__
    // The staged playlist transaction guard is currently verified only with
    // the native SQLite integration, including implicit rollback detection.
    if (error) *error = tr("This build supports Engine preview but not library imports.");
    return false;
#endif
    if (!validateEngineImportPackage(package, error)) return false;
    const auto db = m_manager->internalCollection()->database();
    QSqlQuery version(db);
    if (!version.exec("SELECT value FROM settings WHERE name='mixxx.schema.version'") ||
            !version.next() || version.value(0).toInt() < 42) {
        if (error) *error = tr("This library needs the Engine import schema update.");
        return false;
    }
    m_media = std::make_unique<EngineMediaResolver>(libraryDirectory, mediaRoot);
    if (m_media->libraryDirectory().isEmpty() || m_media->mediaRoot().isEmpty()) {
        if (error) *error = tr("The selected media folders are unavailable.");
        return false;
    }
    m_uuid = package["sourceUuid"].toString();
    m_tracks = package["tracks"].toArray();
    m_playlists = package["playlists"].toArray();
    m_playlistById.clear(); m_resolvedTracks.clear(); m_details.clear();
    for (const auto& value : m_playlists) {
        const auto item = value.toObject();
        m_playlistById.insert(item["id"].toString(), item);
    }
    m_index = m_importedTracks = m_importedPlaylists = m_attention = 0;
    m_cancelled = false; m_portableRatingsDeferred = false; m_running = true;
    QTimer::singleShot(0, this, &EngineImportCoordinator::step);
    return true;
}
void EngineImportCoordinator::cancel() { m_cancelled = true; }
void EngineImportCoordinator::issue(const QString& entity, const QString& message) {
    ++m_attention;
    if (m_details.size() < 100) m_details.append(entity + ": " + message);
}
void EngineImportCoordinator::step() {
    if (!m_running) return;
    const int total = m_tracks.size() + m_playlists.size();
    if (m_cancelled || m_index >= total) {
        m_running = false;
        emit finished(m_importedTracks, m_importedPlaylists, m_attention, m_cancelled, m_details);
        return;
    }
    if (m_index < m_tracks.size()) importTrack(m_tracks[m_index].toObject());
    else importPlaylist(m_playlists[m_index - m_tracks.size()].toObject());
    ++m_index;
    emit progress(m_index, total);
    QTimer::singleShot(0, this, &EngineImportCoordinator::step);
}
void EngineImportCoordinator::importTrack(const QJsonObject& source) {
    const auto sourceId = source["id"].toString();
    const auto title = source["title"].toString().isEmpty() ? tr("Untitled track") : source["title"].toString();
    const auto media = m_media->resolve(source["relativePath"].toString());
    if (media["status"].toString() != "resolved") {
        issue(title, tr("Audio is missing, unreadable, or outside the selected folder.")); return;
    }
    const auto path = media["path"].toString();
    if (path.isEmpty()) { issue(title, tr("Audio path could not be resolved.")); return; }
    const auto fileBytes = source["fileBytes"].toString();
    if (!fileBytes.isEmpty() && fileBytes.toLongLong() > 0 &&
            QFileInfo(path).size() != fileBytes.toLongLong()) {
        issue(title, tr("Audio size differs from the Engine library; review this file first.")); return;
    }
    const auto db = m_manager->internalCollection()->database();
    EngineImportRegistry registry(db);
    const EngineImportKey key{m_uuid, "track", sourceId};
    std::optional<EngineImportRecord> previous;
    QString error;
    if (!registry.load(key, &previous, &error)) { issue(title, error); return; }
    TrackPointer track;
    bool alreadyPresent = false;
    if (previous) {
        if (!previous->localId || *previous->localId > std::numeric_limits<int>::max()) {
            issue(title, tr("The previous local track was removed; review before restoring it.")); return;
        }
        track = m_manager->getTrackById(TrackId(QVariant(*previous->localId)));
        if (!track || QFileInfo(track->getLocation()).canonicalFilePath() != path) {
            issue(title, tr("The previous local track no longer matches this audio path.")); return;
        }
        alreadyPresent = true;
    } else {
        track = m_manager->getOrAddTrack(TrackRef::fromFilePath(path), &alreadyPresent);
    }
    if (!track) { issue(title, tr("BiteDJ could not load this audio file.")); return; }
    const bool portable = SystemSettings::isOnRemovableMedia(path);
    if (portable && !alreadyPresent) {
        // New files may not have gone through the DAO's reload path yet.
        // Overlay the DJ's portable edits before the ordinary save path can
        // interpret file-tag cues/ratings as new edits to those stores.
        FsCueOverrideStore::applyOverrides(track.get());
        FsMetaOverrideStore::applyOverrides(track.get());
    }
    auto metadataSource = source;
    if (portable) {
        // Ordinary rating edits write portable overrides. Until source-rating
        // adoption has a separate baseline path, do not turn Engine ratings
        // into user overrides or replace an explicit portable zero rating.
        metadataSource.remove("ratingPercent");
        if (source.contains("ratingPercent") && !source["ratingPercent"].isNull() &&
                !m_portableRatingsDeferred) {
            issue(tr("Portable media"), tr("Engine star ratings on removable media are deferred; existing ratings are preserved."));
            m_portableRatingsDeferred = true;
        }
    }
    EngineMetadataImportPlan plan;
    Track::RecordReplaceResult publication = Track::RecordReplaceResult::Stale;
    for (int attempt = 0; attempt < 3 && publication == Track::RecordReplaceResult::Stale; ++attempt) {
        const auto snapshot = track->getRecord();
        plan = planEngineMetadataImport(snapshot, previous ? previous->baseline : QJsonObject{},
                metadataSource, !alreadyPresent);
        publication = track->replaceRecordIfUnchanged(snapshot, plan.record);
    }
    if (publication == Track::RecordReplaceResult::Stale) {
        issue(title, tr("The track kept changing. Retry when editing has finished.")); return;
    }
    // Publish as an ordinary metadata edit BEFORE saving. Failed saves leave
    // the live edit dirty. Never write SQL first and hope a stale clean Track
    // eventually flushes itself. Existing metadata-sync preferences still apply.
    if (m_manager->saveTrack(track) == TrackCollectionManager::SaveTrackResult::Failed) {
        issue(title, tr("Metadata is visible but could not be saved. Retry saving this track.")); return;
    }
    SqlTransaction tx(db);
    std::optional<EngineImportRecord> latest;
    if (!tx || !registry.load(key, &latest, &error) || !sameRecord(previous, latest)) {
        issue(title, error.isEmpty() ? tr("Import history changed; retry to record this saved edit.") : error); return;
    }
    QSqlQuery exists(db);
    exists.prepare("SELECT 1 FROM library WHERE id=?"); exists.addBindValue(track->getId().toVariant());
    if (!exists.exec() || !exists.next()) { issue(title, tr("The local track was removed during import.")); return; }
    exists.finish();
    QJsonObject timing;
    for (const auto* field : {"sampleRate", "sampleCount", "bpm", "mainCueFrame", "hotCues", "loops", "beatgrid", "relativePath", "fileBytes", "ratingPercent"})
        timing.insert(field, source[field]);
    plan.baseline.insert("deferredTiming", timing);
    if (!registry.save(key, {track->getId().toVariant().toLongLong(), plan.baseline}, &error) || !tx.commit()) {
        issue(title, tr("Metadata was saved, but its import history was not. Retry the import. %1").arg(error)); return;
    }
    m_resolvedTracks.insert(sourceId, track->getId());
    ++m_importedTracks;
    if (!plan.conflicts.isEmpty()) issue(title, tr("Kept local edits: %1").arg(plan.conflicts.join(", ")));
    if (!plan.unrepresentableFields.isEmpty()) issue(title, tr("Fields need review: %1").arg(plan.unrepresentableFields.join(", ")));
}
QString EngineImportCoordinator::playlistName(const QJsonObject& source) const {
    QStringList path;
    auto item = source;
    int length = 0;
    while (!item.isEmpty()) {
        auto title = item["title"].toString().trimmed();
        if (title.isEmpty()) title = tr("Untitled playlist");
        length += title.size() + 3;
        if (length > 1024) return {};
        path.prepend(title);
        item = m_playlistById.value(item["parentId"].toString());
    }
    return path.join(" / ");
}
void EngineImportCoordinator::importPlaylist(const QJsonObject& source) {
    const auto name = playlistName(source);
    if (name.isEmpty()) { issue(tr("Playlist"), tr("The folder path is too long.")); return; }
    QVector<EnginePlaylistImportEntry> incoming;
    for (const auto& value : source["tracks"].toArray()) {
        const auto entry = value.toObject();
        const auto id = entry["trackId"].toString();
        if (entry["sourceUuid"].toString() != m_uuid || !m_resolvedTracks.contains(id)) {
            issue(name, tr("Deferred because at least one track is unavailable. No entries were omitted.")); return;
        }
        incoming.append({entry["entryId"].toString(), m_resolvedTracks.value(id)});
    }
    auto* collection = m_manager->internalCollection();
    auto& dao = collection->getPlaylistDAO();
    const auto db = collection->database();
    SqlTransaction tx(db);
    EngineImportRegistry registry(db);
    const EngineImportKey key{m_uuid, "playlist", source["id"].toString()};
    QString error;
    std::optional<EngineImportRecord> previous;
    if (!tx || !registry.load(key, &previous, &error)) { issue(name, error); return; }
    PlaylistDAO::StagedPlaylist current;
    QHash<QString, qint64> bindings;
    if (previous) {
        if (!previous->localId || *previous->localId > std::numeric_limits<int>::max()) {
            issue(name, tr("The previous playlist was removed or its old identity needs review.")); return;
        }
        current.id = int(*previous->localId);
        QSqlQuery query(db);
        query.prepare("SELECT name,locked,hidden FROM Playlists WHERE id=?"); query.addBindValue(current.id);
        if (!query.exec() || !query.next() || query.value(1).toBool() || query.value(2).toInt() != 0) {
            issue(name, tr("The local playlist is missing, locked, or hidden.")); return;
        }
        current.name = query.value(0).toString(); query.finish();
        query.prepare("SELECT id,track_id FROM PlaylistTracks WHERE playlist_id=? ORDER BY position,id");
        query.addBindValue(current.id);
        if (!query.exec()) { issue(name, query.lastError().text()); return; }
        while (query.next()) { current.entryIds.append(query.value(0).toLongLong()); current.tracks.append(TrackId(query.value(1))); }
        if (query.lastError().isValid()) { issue(name, query.lastError().text()); return; }
        query.finish();
        query.prepare("SELECT source_id,local_id FROM engine_import_entities WHERE library_uuid=? AND entity_kind='entry' AND local_id IN (SELECT id FROM PlaylistTracks WHERE playlist_id=?)");
        query.addBindValue(m_uuid); query.addBindValue(current.id);
        if (!query.exec()) { issue(name, query.lastError().text()); return; }
        while (query.next()) bindings.insert(query.value(0).toString(), query.value(1).toLongLong());
        if (query.lastError().isValid()) { issue(name, query.lastError().text()); return; }
    }
    QString mappedName;
    const auto oldBaseline = previous ? previous->baseline : QJsonObject{};
    const auto oldSourceName = oldBaseline.value("sourcePathName").toString(oldBaseline.value("name").toString());
    if (previous && oldSourceName == name) {
        // The accepted name may contain an automatically assigned suffix.
        // An unchanged source name must not undo that mapping or a local rename.
        mappedName = oldBaseline.value("name").toString();
    } else {
        QSqlQuery collision(db);
        collision.prepare("SELECT id FROM Playlists WHERE name=? AND id!=?");
        for (int suffix = 1; suffix <= 1000; ++suffix) {
            const auto candidate = suffix == 1 ? name : name + QString(" (%1)").arg(suffix);
            collision.bindValue(0, candidate); collision.bindValue(1, current.id);
            if (!collision.exec()) { issue(name, collision.lastError().text()); return; }
            if (!collision.next()) {
                if (collision.lastError().isValid()) { issue(name, collision.lastError().text()); return; }
                mappedName = candidate; break;
            }
            collision.finish();
        }
        if (mappedName.isEmpty()) { issue(name, tr("Too many matching playlist names. Rename a playlist and retry.")); return; }
    }
    const auto plan = planEnginePlaylistImport(current, oldBaseline,
            bindings, mappedName, incoming, !previous);
    if (!plan.error.isEmpty()) { issue(name, plan.error); return; }
    PlaylistDAO::StagedPlaylist staged;
    if (!previous) {
        if (!dao.stageNewPlaylist(tx, plan.name, plan.tracks, &staged, &error)) { issue(name, error); return; }
    } else {
        if (!dao.stagePlaylistUpdate(tx, current, plan.name, plan.tracks, plan.entryIds, &staged, &error)) { issue(name, error); return; }
    }
    // Save bindings only for source occurrences actually accepted. On content
    // conflict/local-only edits, the unchanged bindings remain authoritative.
    if (plan.acceptsSourceEntries) {
        if (staged.entryIds.size() != incoming.size()) {
            issue(name, tr("Playlist occurrence count changed unexpectedly.")); return;
        }
        for (qsizetype i = 0; i < incoming.size(); ++i)
            if (!registry.save({m_uuid, "entry", incoming[i].sourceEntryId},
                        {staged.entryIds[i], {}}, &error)) { issue(name, error); return; }
    }
    auto acceptedBaseline = plan.baseline;
    if (!plan.conflicts.contains("name")) acceptedBaseline.insert("sourcePathName", name);
    if (!registry.save(key, {staged.id, acceptedBaseline}, &error) || !dao.commitStagedPlaylists(tx, {staged}, &error)) {
        issue(name, error); return;
    }
    ++m_importedPlaylists;
    if (!plan.conflicts.isEmpty()) issue(name, tr("Kept local playlist edits: %1").arg(plan.conflicts.join(", ")));
}
} // namespace mixxx

#include "moc_engineimportcoordinator.cpp"
