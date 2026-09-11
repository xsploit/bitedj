#pragma once
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "library/dao/playlistdao.h"

namespace mixxx {
struct EnginePlaylistImportEntry {
    QString sourceEntryId;
    TrackId trackId; // Already resolved in the caller's source-library context.
};
struct EnginePlaylistImportPlan {
    QString name;
    QList<TrackId> tracks;
    QList<qint64> entryIds; // Zero inserts a new occurrence; others retain identity.
    QJsonObject baseline;
    QStringList conflicts;
    QString error;
    bool acceptsSourceEntries = false;
};
// Detached three-way merge of name and ordered occurrences. A concurrent local
// and source content edit conflicts as a whole; it never guesses how to combine
// two reorders/deletions. Local-only additions/changes survive unchanged source.
// Bindings belong to this source library+playlist. Load fresh non-null bindings
// from schema42+ inside the apply transaction; do not reuse a cached mapping.
// Deletion triggers invalidate row identities before SQLite can recycle them.
// The caller must capture ordered rows (not getTrackIds, which uses DISTINCT),
// validate package/track resolution, and stage against this same exact snapshot.
// Save returned baseline and new occurrence bindings in the same transaction.
// Folder mapping, whole-playlist deletion and explicit conflict decisions are
// separate coordinator responsibilities. This function performs no SQL or I/O.
EnginePlaylistImportPlan planEnginePlaylistImport(
        const PlaylistDAO::StagedPlaylist& current,
        const QJsonObject& acceptedBaseline,
        const QHash<QString, qint64>& occurrenceBindings,
        const QString& incomingName,
        const QVector<EnginePlaylistImportEntry>& incoming,
        bool newEntity = false);
} // namespace mixxx
