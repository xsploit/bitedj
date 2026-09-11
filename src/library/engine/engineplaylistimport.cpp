#include "library/engine/engineplaylistimport.h"

#include <QJsonArray>
#include <QSet>

#include "library/engine/engineimportmerge.h"

namespace mixxx {
EnginePlaylistImportPlan planEnginePlaylistImport(
        const PlaylistDAO::StagedPlaylist& current,
        const QJsonObject& acceptedBaseline,
        const QHash<QString, qint64>& occurrenceBindings,
        const QString& incomingName,
        const QVector<EnginePlaylistImportEntry>& incoming,
        bool newEntity) {
    EnginePlaylistImportPlan result{current.name, current.tracks,
            current.entryIds, acceptedBaseline, {}, {}};
    const auto fail = [&](const QString& error) {
        result.error = error;
        return result;
    };
    if (current.tracks.size() != current.entryIds.size() ||
            (newEntity && (current.id != kInvalidPlaylistId || !current.name.isEmpty() ||
                                 !current.tracks.isEmpty() || !occurrenceBindings.isEmpty() ||
                                 !acceptedBaseline.isEmpty()))) {
        return fail(QStringLiteral("Invalid playlist snapshot or new-entity state"));
    }
    if ((acceptedBaseline.contains("name") && !acceptedBaseline["name"].isString()) ||
            (acceptedBaseline.contains("entries") && !acceptedBaseline["entries"].isArray())) {
        return fail(QStringLiteral("Invalid playlist baseline"));
    }
    QSet<QString> baselineIds;
    for (const auto& value : acceptedBaseline["entries"].toArray()) {
        const auto entry = value.toObject();
        const auto sourceId = entry["sourceEntryId"].toString();
        const auto trackText = entry["trackId"].toString();
        bool validTrack = false;
        const auto trackNumber = trackText.toInt(&validTrack);
        if (!value.isObject() || sourceId.isEmpty() || baselineIds.contains(sourceId) ||
                !validTrack || trackNumber < 0 || QString::number(trackNumber) != trackText ||
                entry.contains("localEntryId")) {
            return fail(QStringLiteral("Invalid playlist baseline occurrence"));
        }
        baselineIds.insert(sourceId);
    }
    QHash<qint64, QString> sourceByLocal;
    for (auto it = occurrenceBindings.begin(); it != occurrenceBindings.end(); ++it) {
        if (it.key().isEmpty() || it.value() <= 0 || sourceByLocal.contains(it.value())) {
            return fail(QStringLiteral("Invalid or duplicate occurrence binding"));
        }
        sourceByLocal.insert(it.value(), it.key());
    }
    QJsonArray localEntries;
    QSet<qint64> localIds;
    for (qsizetype i = 0; i < current.tracks.size(); ++i) {
        const auto id = current.entryIds[i];
        if (id <= 0 || localIds.contains(id) || !current.tracks[i].isValid()) {
            return fail(QStringLiteral("Invalid or duplicate local occurrence"));
        }
        localIds.insert(id);
        QJsonObject entry{{"trackId", current.tracks[i].toString()}};
        if (sourceByLocal.contains(id)) {
            entry.insert("sourceEntryId", sourceByLocal.value(id));
        } else {
            entry.insert("localEntryId", QString::number(id));
        }
        localEntries.append(entry);
    }
    QJsonArray sourceEntries;
    QSet<QString> sourceIds;
    QList<TrackId> sourceTracks;
    QList<qint64> sourceLocalIds;
    for (const auto& item : incoming) {
        if (item.sourceEntryId.isEmpty() || sourceIds.contains(item.sourceEntryId) ||
                !item.trackId.isValid()) {
            return fail(QStringLiteral("Invalid or duplicate source occurrence"));
        }
        sourceIds.insert(item.sourceEntryId);
        sourceEntries.append(QJsonObject{{"sourceEntryId", item.sourceEntryId},
                {"trackId", item.trackId.toString()}});
        sourceTracks.append(item.trackId);
        // A locally deleted occurrence must receive a fresh ID if restoration
        // is eventually accepted. Never ask the DAO to reuse a missing row ID.
        const auto id = occurrenceBindings.value(item.sourceEntryId);
        sourceLocalIds.append(localIds.contains(id) ? id : 0);
    }
    const QJsonObject local{{"name", current.name}, {"entries", localEntries}};
    const QJsonObject source{{"name", incomingName}, {"entries", sourceEntries}};
    const auto merge = mergeEngineImportFields(newEntity ? local : acceptedBaseline,
            local, source);
    result.name = merge.values.value("name").toString();
    result.baseline = merge.baseline;
    result.conflicts = merge.conflicts;
    if (merge.changedFields.contains(QStringLiteral("entries"))) {
        result.tracks = sourceTracks;
        result.entryIds = sourceLocalIds;
    }
    return result;
}
} // namespace mixxx
