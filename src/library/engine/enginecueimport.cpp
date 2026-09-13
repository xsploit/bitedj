#include "library/engine/enginecueimport.h"

#include <QJsonArray>
#include <QMap>
#include <QSet>

#include "engine/controls/cuecontrol.h"
#include "library/engine/engineimportmerge.h"
#include "util/math.h"

namespace mixxx {
namespace {
QString identity(Cue::EngineOrigin::Bank bank, int slot) {
    return QString(bank == Cue::EngineOrigin::Bank::HotCue ? "hot:" : "loop:") + QString::number(slot);
}
QJsonObject fields(const EngineCueImportItem& cue) {
    return {{"type", static_cast<int>(cue.type)},
            {"bounds", QJsonArray{cue.startFrame, cue.endFrame}},
            {"label", cue.label},
            {"rgb", static_cast<int>(cue.rgb)}};
}
void assignFields(EngineCueImportItem* cue, const QJsonObject& value) {
    cue->type = static_cast<CueType>(value["type"].toInt());
    const auto bounds = value["bounds"].toArray();
    cue->startFrame = bounds[0].toDouble();
    cue->endFrame = bounds[1].toDouble();
    cue->label = value["label"].toString();
    cue->rgb = value["rgb"].toInt();
}
} // namespace
EngineCueImportPlan planEngineCueImport(const QVector<EngineCueImportItem>& current,
        const QJsonObject& baseline,
        const QString& libraryUuid,
        const QString& sourceTrackId,
        const QJsonObject& incoming,
        double decodedFrameLimit,
        const QVector<int>& hotControls,
        const QVector<int>& loopControls) {
    EngineCueImportPlan plan{current, baseline, {}, {}, {}};
    auto fail = [&](const QString& error) {
        return EngineCueImportPlan{current, baseline, {}, {}, error};
    };
    if (libraryUuid.isEmpty() || sourceTrackId.isEmpty() || !util_isfinite(decodedFrameLimit) || decodedFrameLimit <= 0) {
        return fail("Missing source identity or verified decoded frame limit");
    }
    QSet<int> pool;
    for (const auto& controls : {hotControls, loopControls}) {
        for (const int control : controls) {
            if (control < 0 || control >= NUM_HOT_CUES || pool.contains(control))
                return fail("Invalid or overlapping cue control pools");
            pool.insert(control);
        }
    }
    QSet<int> occupied;
    QSet<qint64> databaseIds;
    QMap<QString, int> local;
    for (int i = 0; i < current.size(); ++i) {
        const auto& cue = current[i];
        if (cue.control != Cue::kNoHotCue) {
            if (cue.control < 0 || occupied.contains(cue.control))
                return fail("Duplicate or invalid local cue control");
            occupied.insert(cue.control);
        }
        if (cue.databaseId < 0 || (cue.databaseId && databaseIds.contains(cue.databaseId)))
            return fail("Duplicate or invalid cue database ID");
        if (cue.databaseId)
            databaseIds.insert(cue.databaseId);
        if (!cue.origin || cue.origin->libraryUuid != libraryUuid || cue.origin->trackId != sourceTrackId)
            continue;
        const auto& origin = *cue.origin;
        if (origin.slot < 1 || origin.slot > 8 ||
                (origin.bank != Cue::EngineOrigin::Bank::HotCue && origin.bank != Cue::EngineOrigin::Bank::SavedLoop))
            return fail("Invalid local Engine cue identity");
        const auto key = identity(origin.bank, origin.slot);
        if (local.contains(key))
            return fail("Duplicate local Engine cue identity");
        local.insert(key, i);
    }
    QMap<QString, EngineCueImportItem> source;
    for (const auto* bankName : {"hotCues", "loops"}) {
        if (!incoming[bankName].isArray())
            return fail("Missing complete source cue bank");
        const bool loop = QString(bankName) == "loops";
        for (const auto& value : incoming[bankName].toArray()) {
            const auto cue = value.toObject();
            EngineCueImportItem item;
            item.type = loop ? CueType::Loop : CueType::HotCue;
            item.startFrame = cue[loop ? "startFrame" : "frame"].toDouble(-1);
            item.endFrame = loop ? cue["endFrame"].toDouble(-1) : Cue::kNoPosition;
            item.label = cue["label"].toString();
            const auto rgba = cue["rgba"].toArray();
            if (rgba.size() != 4)
                return fail("Invalid source cue color");
            for (const auto& component : rgba) {
                if (!component.isDouble() || component.toInt(-1) < 0 || component.toInt(-1) > 255 ||
                        component.toDouble() != component.toInt())
                    return fail("Invalid source cue color component");
            }
            item.rgb = (rgba[0].toInt() << 16) | (rgba[1].toInt() << 8) | rgba[2].toInt();
            const auto bank = loop ? Cue::EngineOrigin::Bank::SavedLoop : Cue::EngineOrigin::Bank::HotCue;
            const int slot = cue["slot"].toInt();
            if (slot < 1 || slot > 8)
                return fail("Invalid source cue slot");
            item.origin = Cue::EngineOrigin{libraryUuid, sourceTrackId, bank, slot};
            const auto key = identity(bank, slot);
            if (source.contains(key))
                return fail("Duplicate source cue identity");
            if (!util_isfinite(item.startFrame) || !util_isfinite(item.endFrame) || item.startFrame < 0 ||
                    item.startFrame >= decodedFrameLimit || (loop && (item.endFrame <= item.startFrame || item.endFrame > decodedFrameLimit))) {
                plan.unrepresentableFields.append(key + ":bounds");
                // Keep this identity present but skip merging it below, including
                // source deletion handling. Invalid bounds must not erase a cue.
            }
            if (rgba[3].toInt() != 255)
                plan.unrepresentableFields.append(key + ":alpha");
            source.insert(key, item);
        }
    }
    QSet<QString> allKeys;
    for (auto it = local.begin(); it != local.end(); ++it)
        allKeys.insert(it.key());
    for (auto it = source.begin(); it != source.end(); ++it)
        allKeys.insert(it.key());
    for (auto it = baseline.begin(); it != baseline.end(); ++it)
        allKeys.insert(it.key());
    QStringList ordered = allKeys.values();
    ordered.sort();
    QSet<int> removed;
    QStringList additions;
    for (const auto& key : ordered) {
        if (plan.unrepresentableFields.contains(key + ":bounds"))
            continue;
        const bool hasLocal = local.contains(key);
        const bool hasSource = source.contains(key);
        const QJsonValue previous = baseline.contains(key) ? baseline[key] : QJsonValue(QJsonValue::Null);
        const QJsonValue here = hasLocal ? QJsonValue(fields(current[local[key]])) : QJsonValue(QJsonValue::Null);
        const QJsonValue there = hasSource ? QJsonValue(fields(source[key])) : QJsonValue(QJsonValue::Null);
        // Retain a per-field baseline when both sides still have the cue. Treat
        // existence changes as a whole-cue merge so local deletions/edits survive.
        if (hasLocal && hasSource && previous.isObject() &&
                current[local[key]].type == source[key].type) {
            const auto merged = mergeEngineImportFields(previous.toObject(), here.toObject(), there.toObject());
            assignFields(&plan.cues[local[key]], merged.values);
            plan.baseline.insert(key, merged.baseline);
            for (const auto& field : merged.conflicts)
                plan.conflicts.append(key + ":" + field);
            continue;
        }
        if (here == there) {
            plan.baseline.insert(key, there);
            continue;
        }
        if (here != previous) {
            if (there != previous)
                plan.conflicts.append(key + ":existence");
            continue;
        }
        if (!hasSource) {
            if (hasLocal)
                removed.insert(local[key]);
            plan.baseline.insert(key, QJsonValue(QJsonValue::Null));
            continue;
        }
        if (hasLocal) {
            assignFields(&plan.cues[local[key]], there.toObject());
            plan.baseline.insert(key, there);
            continue;
        }
        additions.append(key);
    }
    for (const int index : removed)
        occupied.remove(current[index].control);
    for (const auto& key : additions) {
        auto item = source[key];
        const auto& controls = item.origin->bank == Cue::EngineOrigin::Bank::HotCue ? hotControls : loopControls;
        int control = Cue::kNoHotCue;
        for (const int candidate : controls) {
            if (!occupied.contains(candidate)) {
                control = candidate;
                break;
            }
        }
        if (control == Cue::kNoHotCue) {
            plan.conflicts.append(key + ":control-capacity");
            continue;
        }
        item.control = control;
        occupied.insert(control);
        plan.cues.append(item);
        plan.baseline.insert(key, fields(item));
    }
    // Erase back-to-front so source identity indexes remain valid during merge.
    for (int i = current.size() - 1; i >= 0; --i) {
        if (removed.contains(i))
            plan.cues.removeAt(i);
    }
    return plan;
}
} // namespace mixxx
