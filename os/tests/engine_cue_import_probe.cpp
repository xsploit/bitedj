#include <QCoreApplication>
#include <QJsonArray>
#include <cstdlib>
#include <iostream>

#include "library/engine/enginecueimport.h"

using namespace mixxx;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        std::exit(1);
    }
}
QJsonObject hot(int slot = 1, double frame = 100.25, const QString& label = "hot") {
    return {{"slot", slot}, {"frame", frame}, {"label", label}, {"rgba", QJsonArray{1, 2, 3, 255}}};
}
QJsonObject loop(int slot = 1, double start = 200.5, double end = 400.75) {
    return {{"slot", slot}, {"startFrame", start}, {"endFrame", end}, {"label", "loop"}, {"rgba", QJsonArray{4, 5, 6, 255}}};
}
QJsonObject input(QJsonArray hotCues = {hot()}, QJsonArray loops = {loop()}) {
    return {{"hotCues", hotCues}, {"loops", loops}};
}
EngineCueImportPlan plan(const QVector<EngineCueImportItem>& cues, const QJsonObject& baseline, const QJsonObject& source, const QVector<int>& hotPool = {0, 1}, const QVector<int>& loopPool = {26, 27}) {
    return planEngineCueImport(cues, baseline, "library", "1", source, 1000, hotPool, loopPool);
}
const EngineCueImportItem& find(const EngineCueImportPlan& p, Cue::EngineOrigin::Bank bank, int slot = 1) {
    for (const auto& cue : p.cues)
        if (cue.origin && cue.origin->bank == bank && cue.origin->slot == slot && cue.origin->libraryUuid == "library")
            return cue;
    check(false, "missing cue");
    std::abort();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using Bank = Cue::EngineOrigin::Bank;
    EngineCueImportItem plain;
    plain.control = 0;
    plain.label = "custom mapping";
    plain.databaseId = 7;
    auto initial = plan({plain}, {}, input());
    check(initial.error.isEmpty() && initial.conflicts.isEmpty() && initial.cues.size() == 3, "initial import");
    check(initial.cues[0].label == plain.label && initial.cues[0].databaseId == 7, "plain cue preserved");
    check(find(initial, Bank::HotCue).control == 1 && find(initial, Bank::SavedLoop).control == 26, "separate banks and occupied control");
    check(find(initial, Bank::HotCue).startFrame == 100.25 && find(initial, Bank::SavedLoop).endFrame == 400.75, "fractional bounds");
    for (int i = 1; i < initial.cues.size(); ++i)
        initial.cues[i].databaseId = i + 10;
    auto repeat = plan(initial.cues, initial.baseline, input());
    check(repeat.cues.size() == 3 && repeat.baseline == initial.baseline && repeat.conflicts.isEmpty(), "repeat stable");
    check(find(repeat, Bank::HotCue).databaseId == find(initial, Bank::HotCue).databaseId, "database identity stable");
    auto local = initial.cues;
    for (auto& cue : local)
        if (cue.origin && cue.origin->bank == Bank::HotCue)
            cue.label = "local label";
    auto independent = plan(local, initial.baseline, input({hot(1, 150.5)}, {loop()}));
    check(independent.conflicts.isEmpty() && find(independent, Bank::HotCue).label == "local label" && find(independent, Bank::HotCue).startFrame == 150.5, "independent field merge");
    auto conflict = plan(local, initial.baseline, input({hot(1, 100.25, "source label")}, {loop()}));
    check(conflict.conflicts.contains("hot:1:label") && find(conflict, Bank::HotCue).label == "local label", "same field conflict");
    auto again = plan(conflict.cues, conflict.baseline, input({hot(1, 100.25, "source label")}, {loop()}));
    check(again.conflicts == conflict.conflicts && again.baseline == conflict.baseline, "repeated conflict retained");
    auto changedType = initial.cues;
    for (auto& cue : changedType)
        if (cue.origin && cue.origin->bank == Bank::HotCue) {
            cue.type = CueType::Loop;
            cue.endFrame = 300;
        }
    auto typeConflict = plan(changedType, initial.baseline, input({hot(1, 350)}, {loop()}));
    check(typeConflict.conflicts.contains("hot:1:existence") && find(typeConflict, Bank::HotCue).startFrame == 100.25 && find(typeConflict, Bank::HotCue).endFrame == 300, "changed type cannot combine with invalid source bounds");
    auto adoptedTypeBaseline = initial.baseline;
    auto adopted = adoptedTypeBaseline["hot:1"].toObject();
    adopted["type"] = static_cast<int>(CueType::Loop);
    adopted["bounds"] = QJsonArray{100.25, 300};
    adoptedTypeBaseline["hot:1"] = adopted;
    auto restoredType = plan(changedType, adoptedTypeBaseline, input());
    check(restoredType.cues.size() == changedType.size() && find(restoredType, Bank::HotCue).type == CueType::HotCue &&
                    find(restoredType, Bank::HotCue).databaseId == find(initial, Bank::HotCue).databaseId,
            "accepted type transition updates existing identity");
    auto erased = plan(initial.cues, initial.baseline, input({}, {}));
    check(erased.cues.size() == 1 && erased.baseline["hot:1"].isNull() && erased.baseline["loop:1"].isNull(), "source deletion");
    auto editedDelete = plan(local, initial.baseline, input({}, {}));
    check(editedDelete.cues.size() == 2 && editedDelete.conflicts.contains("hot:1:existence"), "source deletion protects local edit");
    auto localDelete = plan({plain}, initial.baseline, input());
    check(localDelete.cues.size() == 1 && localDelete.conflicts.isEmpty(), "unchanged source respects local deletion");
    auto localDeleteChange = plan({plain}, initial.baseline, input({hot(1, 160)}, {}));
    check(localDeleteChange.cues.size() == 1 && localDeleteChange.conflicts.contains("hot:1:existence"), "changed source conflicts local deletion");
    auto recreated = plan(erased.cues, erased.baseline, input());
    check(recreated.cues.size() == 3 && recreated.conflicts.isEmpty(), "recreate after accepted source deletion");
    auto full = plan({plain}, {}, input(), {0}, {});
    check(full.cues.size() == 1 && full.conflicts.size() == 2 && full.baseline.isEmpty(), "capacity conflict no baseline advance");
    // A removed slot frees its control for a differently numbered source cue.
    auto replacement = plan(initial.cues, initial.baseline, input({hot(2)}, {loop(2)}), {1}, {26});
    check(replacement.conflicts.isEmpty() && replacement.cues.size() == 3, "deletion frees capacity in same plan");
    check(find(replacement, Bank::HotCue, 2).control == 1, "replacement reuses available control");
    auto outside = plan(initial.cues, initial.baseline, input({hot(1, 1000)}, {loop(1, 200, 1001)}));
    check(outside.unrepresentableFields.size() == 2 && outside.baseline == initial.baseline && outside.cues.size() == 3, "bad media bounds preserve prior state");
    check(plan({}, {}, input({}, {loop(1, 200, 1000)})).unrepresentableFields.isEmpty(), "loop may end at exact frame limit");
    auto duplicate = initial.cues;
    duplicate.append(initial.cues[1]);
    check(!plan(duplicate, initial.baseline, input()).error.isEmpty(), "duplicate local identity rejected");
    check(!plan({}, {}, input(), {0}, {0}).error.isEmpty(), "overlapping pools rejected");
    check(!plan({}, {}, input(), {37}, {}).error.isEmpty(), "out-of-range pool rejected");
    EngineCueImportItem other = plain;
    other.control = 26;
    other.databaseId = 8;
    other.origin = Cue::EngineOrigin{"other-library", "1", Bank::SavedLoop, 1};
    auto foreign = plan({other}, {}, input());
    check(foreign.cues[0].origin->libraryUuid == "other-library" && find(foreign, Bank::SavedLoop).control == 27, "foreign provenance and occupied control retained");
    auto translucent = hot();
    translucent["rgba"] = QJsonArray{1, 2, 3, 128};
    auto alpha = plan({}, {}, input({translucent}, {}));
    check(alpha.unrepresentableFields.contains("hot:1:alpha") && find(alpha, Bank::HotCue).rgb == 0x010203, "alpha limitation explicit");
    std::cout << "PASS Engine cue merge: distinct banks, occupied controls, identities, fractional bounds, idempotence, field conflicts, source/local deletion, recreation, capacity, frame bounds and alpha reporting\n";
}
