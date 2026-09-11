#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "track/cue.h"

namespace mixxx {
// Detached values, captured consistently by the caller. No QObject crosses the
// planning boundary; database identity and local control remain independent of
// the original Engine bank/slot.
struct EngineCueImportItem {
    qint64 databaseId = 0;
    int control = Cue::kNoHotCue;
    CueType type = CueType::HotCue;
    double startFrame = 0;
    double endFrame = Cue::kNoPosition;
    QString label;
    quint32 rgb = 0;
    std::optional<Cue::EngineOrigin> origin;
};
struct EngineCueImportPlan {
    QVector<EngineCueImportItem> cues;
    QJsonObject baseline;
    QStringList conflicts;
    QStringList unrepresentableFields;
    QString error;
};
// Baselines are scoped to one source library+track; keys are hot:1..8/loop:1..8.
// Incoming is a validated reader track. The caller must first establish matching
// source/local sample rate AND frame origin, and supply the decoded frame limit.
// No rescaling or encoder-delay guess is made here. Explicit control pools must
// exclude reserved/custom-mapping controls. Occupied controls are never replaced.
// On error the original list/baseline are returned. Conflicts retain local data.
EngineCueImportPlan planEngineCueImport(const QVector<EngineCueImportItem>& current,
        const QJsonObject& baseline,
        const QString& libraryUuid,
        const QString& sourceTrackId,
        const QJsonObject& incoming,
        double decodedFrameLimit,
        const QVector<int>& hotControls,
        const QVector<int>& loopControls);
} // namespace mixxx
