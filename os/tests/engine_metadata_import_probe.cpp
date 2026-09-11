#include <QCoreApplication>
#include <QDateTime>
#include <iostream>
#include <stdexcept>

#include "library/engine/enginemetadataimport.h"
#include "track/keyutils.h"

static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        using namespace mixxx;
        TrackRecord current(TrackId(QVariant(91)));
        auto& info = current.refMetadata().refTrackInfo();
        info.setTitle("File title");
        info.setGrouping("Keep grouping");
        info.setTrackTotal("12");
        current.refMetadata().refAlbumInfo().setArtist("Keep album artist");
        current.setDateAdded(QDateTime::fromSecsSinceEpoch(123456789));
        current.setMainCuePosition(audio::FramePos(123.5));
        current.setBpmLocked(true);
        QJsonObject incoming{{"sourceTitle", "Engine title"}, {"artist", "Artist"}, {"album", "Album"}, {"genre", "Genre"}, {"comment", "VIP"}, {"composer", "Composer"}, {"year", 2026}, {"trackNumber", 4}, {"ratingPercent", 60}, {"keyId", 1}, {"sampleRate", 48000}, {"durationMs", 99999}, {"fileBytes", "9007199254740993"}};
        const auto plan = planEngineMetadataImport(current, {}, incoming, true);
        check(current.getMetadata().getTrackInfo().getTitle() == "File title", "Changed original record");
        check(plan.record.getMetadata().getTrackInfo().getTitle() == "Engine title", "New record title not initialized");
        check(plan.record.getRating() == 3, "Rating percent not mapped to stars");
        check(plan.record.getKeys().getGlobalKey() == track::io::key::A_MINOR, "Engine key1 wrongly cast");
        check(plan.record.getId() == current.getId() && plan.record.getDateAdded() == current.getDateAdded(), "Identity/date changed");
        check(plan.record.getMainCuePosition() == current.getMainCuePosition() && plan.record.getBpmLocked(), "Unmanaged cue/BPM changed");
        check(plan.record.getMetadata().getStreamInfo() == current.getMetadata().getStreamInfo(), "Read-only audio properties changed");
        check(plan.record.getMetadata().getTrackInfo().getGrouping() == "Keep grouping" &&
                        plan.record.getMetadata().getTrackInfo().getTrackTotal() == "12" &&
                        plan.record.getMetadata().getAlbumInfo().getArtist() == "Keep album artist",
                "Unrelated metadata overwritten");
        const auto repeated = planEngineMetadataImport(plan.record, plan.baseline, incoming);
        check(repeated.changedFields.isEmpty() && repeated.conflicts.isEmpty(), "Repeat not idempotent");
        check(repeated.record.getKeys().getGlobalKey() == plan.record.getKeys().getGlobalKey(), "Repeat changed key");
        auto local = plan.record;
        local.refMetadata().refTrackInfo().setTitle("Local edit");
        auto localOnly = planEngineMetadataImport(local, plan.baseline, incoming);
        check(localOnly.record.getMetadata().getTrackInfo().getTitle() == "Local edit" && localOnly.conflicts.isEmpty(), "Local-only edit lost");
        incoming.insert("sourceTitle", "Source edit");
        incoming.insert("comment", "Source comment");
        auto conflict = planEngineMetadataImport(local, plan.baseline, incoming);
        check(conflict.conflicts == QStringList{"title"} && conflict.record.getMetadata().getTrackInfo().getTitle() == "Local edit", "Concurrent title overwritten");
        check(conflict.record.getMetadata().getTrackInfo().getComment() == "Source comment", "Independent source edit lost");
        auto retry = planEngineMetadataImport(conflict.record, conflict.baseline, incoming);
        check(retry.conflicts == conflict.conflicts && retry.changedFields.isEmpty(), "Repeated conflict vanished");
        QJsonObject legacy{{"title", "Display fallback"}};
        auto omitted = planEngineMetadataImport(current, {}, legacy);
        check(omitted.changedFields.isEmpty() && omitted.record.getMetadata().getTrackInfo().getTitle() == "File title", "Legacy display title used as source clear");
        auto clear = planEngineMetadataImport(plan.record, plan.baseline, {{"sourceTitle", QJsonValue::Null}});
        check(clear.record.getMetadata().getTrackInfo().getTitle().isEmpty() && clear.conflicts.isEmpty(), "Managed title clear failed");
        auto unowned = planEngineMetadataImport(current, {}, {{"sourceTitle", QJsonValue::Null}});
        check(unowned.conflicts == QStringList{"title"} && unowned.record.getMetadata().getTrackInfo().getTitle() == "File title", "Unowned title cleared");
        auto invalidRating = planEngineMetadataImport(plan.record, plan.baseline, {{"ratingPercent", 33}});
        check(invalidRating.unrepresentableFields == QStringList{"ratingPercent"} && invalidRating.record.getRating() == 3 && invalidRating.baseline == plan.baseline, "Unrepresentable rating rounded or accepted");
        TrackRecord customKey;
        customKey.refMetadata().refTrackInfo().setKeyText("Custom key note");
        auto protectedKey = planEngineMetadataImport(customKey, {}, {{"keyId", 1}});
        check(protectedKey.conflicts == QStringList{"key"} && protectedKey.record.getMetadata().getTrackInfo().getKeyText() == "Custom key note", "Unrecognized local key text overwritten");
        const QStringList expected{"C", "Am", "G", "Em", "D", "Bm", "A", "F#m", "E", "C#m", "B", "G#m", "F#", "Ebm", "Db", "Bbm", "Ab", "Fm", "Eb", "Cm", "Bb", "Gm", "F", "Dm"};
        for (int k = 0; k < expected.size(); ++k) {
            auto keyed = planEngineMetadataImport(TrackRecord(), {}, {{"keyId", k}}, true);
            check(keyed.record.getKeys().getGlobalKey() == KeyUtils::guessKeyFromText(expected[k]), "Circle-of-fifths key mapping differs from parsed musical name");
        }
        for (int stars = 0; stars <= 5; ++stars) {
            auto rated = planEngineMetadataImport(TrackRecord(), {}, {{"ratingPercent", stars * 20}}, true);
            check(rated.record.getRating() == stars, "Rating level wrong");
        }
        std::cout << "PASS actual TrackRecord planning: 24 musical names, 6 rating levels, metadata/identity isolation, initial/reimport/local edits/conflicts/null/omission/unrepresentable values\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
