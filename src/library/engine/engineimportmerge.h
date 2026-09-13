#pragma once
#include <QChar>
#include <QJsonObject>
#include <QStringList>

namespace mixxx {
struct EngineImportMerge {
    QJsonObject values;
    QJsonObject baseline;
    QStringList changedFields;
    QStringList conflicts;
};
// Missing incoming fields are unmanaged, not deletion requests. Explicit JSON
// null is a value. Never advance a conflicting baseline: repeated imports must
// continue reporting the conflict until it is resolved.
EngineImportMerge mergeEngineImportFields(const QJsonObject& baseline,
        const QJsonObject& local,
        const QJsonObject& incoming);
} // namespace mixxx
