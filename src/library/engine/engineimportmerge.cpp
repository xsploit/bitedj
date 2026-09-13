#include "library/engine/engineimportmerge.h"

namespace mixxx {
EngineImportMerge mergeEngineImportFields(const QJsonObject& baseline,
        const QJsonObject& local,
        const QJsonObject& incoming) {
    EngineImportMerge result{local, baseline, {}, {}};
    for (auto it = incoming.begin(); it != incoming.end(); ++it) {
        const auto key = it.key();
        const auto source = it.value();
        const auto previous = baseline.value(key);
        const auto current = local.value(key);
        if (current == source) {
            result.baseline.insert(key, source);
        } else if (current == previous) {
            result.values.insert(key, source);
            result.baseline.insert(key, source);
            result.changedFields.append(key);
        } else if (source != previous) {
            result.conflicts.append(key);
        }
    }
    return result;
}
} // namespace mixxx
