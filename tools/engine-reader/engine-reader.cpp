#include <djinterop/djinterop.hpp>
#include <djinterop/engine/v3/engine_library.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

// Audit reader only. Invoke on a disposable copy, never an active library.
static QJsonArray color(const djinterop::pad_color& c) {
    return {int(c.r), int(c.g), int(c.b), int(c.a)};
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        auto db = djinterop::engine::load_database(argv[1]);
        auto rawLibrary = djinterop::engine::v3::engine_library::load(argv[1]);
        auto performanceData = rawLibrary.performance_data();
        QJsonArray tracks;
        for (const auto& t : db.tracks()) {
            const auto s = t.snapshot();
            const auto rawCues = performanceData.get_quick_cues(t.id());
            QJsonArray cues, loops, collisions, grid;
            for (size_t i=0; i<s.hot_cues.size(); ++i) {
                if (!s.hot_cues[i]) continue;
                const auto& c = *s.hot_cues[i];
                cues.append(QJsonObject{{"slot", int(i+1)}, {"label", QString::fromStdString(c.label)},
                        {"frame", c.sample_offset}, {"rgba", color(c.color)}});
                if (i<s.loops.size() && s.loops[i]) collisions.append(int(i+1));
            }
            for (size_t i=0; i<s.loops.size(); ++i) {
                if (!s.loops[i]) continue;
                const auto& c = *s.loops[i];
                loops.append(QJsonObject{{"slot", int(i+1)}, {"label", QString::fromStdString(c.label)},
                        {"startFrame", c.start_sample_offset}, {"endFrame", c.end_sample_offset},
                        {"rgba", color(c.color)}});
            }
            for (const auto& b : s.beatgrid) grid.append(QJsonObject{{"index", b.index}, {"frame", b.sample_offset}});
            tracks.append(QJsonObject{{"id", QString::number(t.id())},
                    {"title", QString::fromStdString(s.title.value_or(""))},
                    {"sourceTitle", s.title ? QJsonValue(QString::fromStdString(*s.title)) : QJsonValue()},
                    {"artist", s.artist ? QJsonValue(QString::fromStdString(*s.artist)) : QJsonValue()},
                    {"album", s.album ? QJsonValue(QString::fromStdString(*s.album)) : QJsonValue()},
                    {"genre", s.genre ? QJsonValue(QString::fromStdString(*s.genre)) : QJsonValue()},
                    {"comment", s.comment ? QJsonValue(QString::fromStdString(*s.comment)) : QJsonValue()},
                    {"composer", s.composer ? QJsonValue(QString::fromStdString(*s.composer)) : QJsonValue()},
                    {"publisher", s.publisher ? QJsonValue(QString::fromStdString(*s.publisher)) : QJsonValue()},
                    {"keyId", s.key ? QJsonValue(static_cast<int>(*s.key)) : QJsonValue()},
                    {"bitrateKbps", s.bitrate ? QJsonValue(*s.bitrate) : QJsonValue()},
                    {"ratingPercent", s.rating ? QJsonValue(*s.rating) : QJsonValue()},
                    {"year", s.year ? QJsonValue(*s.year) : QJsonValue()},
                    {"trackNumber", s.track_number ? QJsonValue(*s.track_number) : QJsonValue()},
                    {"fileBytes", s.file_bytes ? QJsonValue(QString::number(*s.file_bytes)) : QJsonValue()},
                    {"bpm", s.bpm ? QJsonValue(*s.bpm) : QJsonValue()},
                    {"durationMs", s.duration ? QJsonValue(double(s.duration->count())) : QJsonValue()},
                    {"mainCueFrame", s.main_cue ? QJsonValue(*s.main_cue) : QJsonValue()},
                    // Preserve the source state without guessing which value
                    // native playback selects or translating decoder origins.
                    {"mainCueState", QJsonObject{{"defaultFrame", rawCues.default_main_cue},
                                             {"adjustedFrame", rawCues.adjusted_main_cue},
                                             {"isAdjusted", rawCues.is_main_cue_adjusted}}},
                    {"relativePath", QString::fromStdString(s.relative_path.value_or(""))},
                    {"sampleCount", s.sample_count ? QJsonValue(QString::number(*s.sample_count)) : QJsonValue()},
                    {"sampleRate", s.sample_rate.value_or(0)}, {"hotCues", cues}, {"loops", loops},
                    {"beatgrid", grid}, {"sameSlotCollisions", collisions}});
        }
        QJsonObject result{{"sourceUuid", QString::fromStdString(db.uuid())}, {"schema", QString::fromStdString(db.version_name())}, {"tracks", tracks}};
        std::cout << QJsonDocument(result).toJson().constData();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
