#include "library/engine/enginemetadataimport.h"

#include <array>

#include "library/engine/engineimportmerge.h"
#include "track/keyfactory.h"

namespace mixxx {
namespace {
using namespace track::io::key;
// libdjinterop's circle-of-fifths ordering differs from BiteDJ's chromatic enum.
constexpr std::array<ChromaticKey, 24> kKeys = {C_MAJOR, A_MINOR, G_MAJOR, E_MINOR, D_MAJOR, B_MINOR, A_MAJOR, F_SHARP_MINOR, E_MAJOR, C_SHARP_MINOR, B_MAJOR, G_SHARP_MINOR, F_SHARP_MAJOR, E_FLAT_MINOR, D_FLAT_MAJOR, B_FLAT_MINOR, A_FLAT_MAJOR, F_MINOR, E_FLAT_MAJOR, C_MINOR, B_FLAT_MAJOR, G_MINOR, F_MAJOR, D_MINOR};
QJsonObject localFields(const TrackRecord& record) {
    const auto& info = record.getMetadata().getTrackInfo();
    QJsonObject result{{"title", info.getTitle()}, {"artist", info.getArtist()}, {"album", record.getMetadata().getAlbumInfo().getTitle()}, {"genre", info.getGenre()}, {"comment", info.getComment()}, {"composer", info.getComposer()}, {"year", info.getYear()}, {"trackNumber", info.getTrackNumber()}, {"rating", record.getRating()}, {"key", static_cast<int>(record.getKeys().getGlobalKey())}};
    if (record.getKeys().getGlobalKey() == INVALID && !info.getKeyText().isEmpty()) {
        // An unrecognized local key label is still user data, not an empty key.
        result.insert("key", info.getKeyText());
    }
#ifdef __EXTRA_METADATA__
    result.insert("publisher", record.getMetadata().getAlbumInfo().getRecordLabel());
#endif
    return result;
}
bool emptyField(const QJsonValue& value) {
    return value.isNull() || (value.isString() && value.toString().isEmpty()) ||
            (value.isDouble() && value.toDouble() == 0);
}
} // namespace
EngineMetadataImportPlan planEngineMetadataImport(const TrackRecord& current,
        const QJsonObject& acceptedBaseline,
        const QJsonObject& incoming,
        bool newEntity) {
    EngineMetadataImportPlan plan{current, acceptedBaseline, {}, {}, {}};
    QJsonObject source;
    for (const auto* name : {"sourceTitle", "artist", "album", "genre", "comment", "composer", "publisher"}) {
        if (!incoming.contains(name))
            continue;
        const QString field = QString(name) == "sourceTitle" ? "title" : name;
#ifndef __EXTRA_METADATA__
        if (field == "publisher") {
            if (!incoming[name].isNull())
                plan.unrepresentableFields.append(field);
            continue;
        }
#endif
        source.insert(field, incoming[name].toString());
    }
    for (const auto* name : {"year", "trackNumber"}) {
        if (incoming.contains(name)) {
            source.insert(name, incoming[name].isNull() ? QString() : QString::number(incoming[name].toInt()));
        }
    }
    if (incoming.contains("keyId")) {
        const auto value = incoming["keyId"];
        if (value.isNull())
            source.insert("key", static_cast<int>(INVALID));
        else if (value.isDouble() && value.toInt(-1) >= 0 && value.toInt(-1) < 24 && value.toDouble() == value.toInt()) {
            source.insert("key", static_cast<int>(kKeys[value.toInt()]));
        } else
            plan.unrepresentableFields.append("keyId");
    }
    if (incoming.contains("ratingPercent")) {
        const auto value = incoming["ratingPercent"];
        if (value.isNull())
            source.insert("rating", 0);
        else if (value.isDouble() && value.toInt(-1) >= 0 && value.toInt(-1) <= 100 &&
                value.toInt() % 20 == 0 && value.toDouble() == value.toInt()) {
            source.insert("rating", value.toInt() / 20);
        } else
            plan.unrepresentableFields.append("ratingPercent");
    }
    const auto local = localFields(current);
    auto baseline = acceptedBaseline;
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (!baseline.contains(it.key()) && (newEntity || emptyField(local[it.key()]))) {
            baseline.insert(it.key(), local[it.key()]);
        }
    }
    const auto merge = mergeEngineImportFields(baseline, local, source);
    plan.baseline = merge.baseline;
    plan.changedFields = merge.changedFields;
    plan.conflicts = merge.conflicts;
    auto& metadata = plan.record.refMetadata();
    auto& info = metadata.refTrackInfo();
    // Change only accepted changed fields. Rebuilding all fields would discard
    // unrelated key analyses or normalized tag state on an idempotent import.
    for (const auto& field : merge.changedFields) {
        const auto value = merge.values[field];
        if (field == "title")
            info.setTitle(value.toString());
        else if (field == "artist")
            info.setArtist(value.toString());
        else if (field == "album")
            metadata.refAlbumInfo().setTitle(value.toString());
        else if (field == "genre")
            info.setGenre(value.toString());
        else if (field == "comment")
            info.setComment(value.toString());
        else if (field == "composer")
            info.setComposer(value.toString());
        else if (field == "year")
            info.setYear(value.toString());
        else if (field == "trackNumber")
            info.setTrackNumber(value.toString());
#ifdef __EXTRA_METADATA__
        else if (field == "publisher")
            metadata.refAlbumInfo().setRecordLabel(value.toString());
#endif
        else if (field == "rating")
            plan.record.setRating(value.toInt());
        else if (field == "key") {
            plan.record.setKeys(KeyFactory::makeBasicKeys(static_cast<ChromaticKey>(value.toInt()), FILE_METADATA));
        }
    }
    return plan;
}
} // namespace mixxx
