#pragma once
#include <QChar>
#include <QJsonObject>
#include <QStringList>

#include "track/trackrecord.h"

namespace mixxx {
struct EngineMetadataImportPlan {
    TrackRecord record;
    // Baselines use normalized BiteDJ field values, not raw reader values.
    QJsonObject baseline;
    QStringList changedFields;
    QStringList conflicts;
    QStringList unrepresentableFields;
};
// Input is a track from an already validated Engine package. This copies the
// record; it never changes a live Track, DAO, cue list, audio properties or tags.
// An existing unowned nonempty field requires agreement or conflict resolution.
// A new entity may initialize fields from Engine, including over file-tag values.
EngineMetadataImportPlan planEngineMetadataImport(const TrackRecord& current,
        const QJsonObject& acceptedBaseline,
        const QJsonObject& incoming,
        bool newEntity = false);
} // namespace mixxx
