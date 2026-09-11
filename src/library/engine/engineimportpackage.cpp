#include "library/engine/engineimportpackage.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <stdexcept>

#include "util/fpclassify.h"

namespace mixxx {
namespace {
void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::invalid_argument(message.toStdString());
    }
}
bool text(const QJsonValue& value, bool nonempty = false) {
    return value.isString() && value.toString().size() <= 65536 &&
            !value.toString().contains(QChar(0)) &&
            (!nonempty || !value.toString().isEmpty());
}
bool number(const QJsonValue& value) {
    return value.isDouble() && util_isfinite(value.toDouble());
}
bool integer(const QJsonValue& value, double minimum, double maximum) {
    return number(value) && value.toDouble() >= minimum &&
            value.toDouble() <= maximum && std::floor(value.toDouble()) == value.toDouble();
}
bool identity(const QJsonValue& value) {
    if (!text(value, true)) {
        return false;
    }
    bool ok;
    const auto id = value.toString().toLongLong(&ok);
    return ok && id > 0 && QString::number(id) == value.toString();
}
bool sampleCount(const QJsonValue& value) {
    if (value.isNull()) {
        return true;
    }
    if (!text(value, true)) {
        return false;
    }
    bool ok;
    const auto count = value.toString().toULongLong(&ok);
    return ok && QString::number(count) == value.toString();
}
void validateTrack(const QJsonObject& track) {
    for (const auto* key : {"title", "relativePath"}) {
        require(text(track[key]), QString("Invalid track %1").arg(key));
    }
    for (const auto* key : {"artist", "album", "genre"}) {
        require(track[key].isNull() || text(track[key]), QString("Invalid track %1").arg(key));
    }
    for (const auto* key : {"sourceTitle", "comment", "composer", "publisher"}) {
        if (track.contains(key)) {
            require(track[key].isNull() || text(track[key]), QString("Invalid track %1").arg(key));
        }
    }
    for (const auto* key : {"keyId", "bitrateKbps", "ratingPercent", "year", "trackNumber"}) {
        if (!track.contains(key) || track[key].isNull()) {
            continue;
        }
        const QString field(key);
        const double minimum = (field == "year" || field == "trackNumber") ? -2147483648.0 : 0.0;
        const double maximum = field == "keyId" ? 23.0 : field == "ratingPercent" ? 100.0
                                                                                  : 2147483647.0;
        require(integer(track[key], minimum, maximum), QString("Invalid track %1").arg(key));
    }
    if (track.contains("fileBytes")) {
        require(sampleCount(track["fileBytes"]), "Invalid file bytes");
    }
    for (const auto* key : {"bpm", "durationMs", "mainCueFrame"}) {
        require(track[key].isNull() || number(track[key]), QString("Invalid track %1").arg(key));
    }
    if (track.contains("mainCueState")) {
        const auto state = track["mainCueState"].toObject();
        require(track["mainCueState"].isObject() && number(state["defaultFrame"]) &&
                        number(state["adjustedFrame"]) && state["isAdjusted"].isBool(),
                "Invalid main cue state");
    }
    require(sampleCount(track["sampleCount"]), "Invalid sample count");
    require(number(track["sampleRate"]) && track["sampleRate"].toDouble() >= 0,
            "Invalid sample rate");
    QSet<int> hotSlots;
    QSet<int> loopSlots;
    for (const auto* bank : {"hotCues", "loops"}) {
        const bool loop = QString::fromLatin1(bank) == "loops";
        require(track[bank].isArray() && track[bank].toArray().size() <= 8,
                "Invalid cue bank");
        auto& bankSlots = loop ? loopSlots : hotSlots;
        for (const auto& value : track[bank].toArray()) {
            require(value.isObject(), "Invalid cue object");
            const auto cue = value.toObject();
            require(integer(cue["slot"], 1, 8), "Invalid cue slot");
            const int slot = cue["slot"].toInt();
            require(!bankSlots.contains(slot), "Duplicate slot within cue bank");
            bankSlots.insert(slot);
            require(text(cue["label"]), "Invalid cue label");
            require(cue["rgba"].isArray() && cue["rgba"].toArray().size() == 4,
                    "Invalid cue color");
            for (const auto& component : cue["rgba"].toArray()) {
                require(integer(component, 0, 255), "Invalid cue color component");
            }
            if (loop) {
                require(number(cue["startFrame"]) && number(cue["endFrame"]) &&
                                cue["startFrame"].toDouble() >= 0 &&
                                cue["endFrame"].toDouble() > cue["startFrame"].toDouble(),
                        "Invalid loop bounds");
            } else {
                require(number(cue["frame"]) && cue["frame"].toDouble() >= 0,
                        "Invalid hot cue position");
            }
        }
    }
    QJsonArray collisions;
    for (int slot = 1; slot <= 8; ++slot) {
        if (hotSlots.contains(slot) && loopSlots.contains(slot)) {
            collisions.append(slot);
        }
    }
    require(track["sameSlotCollisions"].isArray() &&
                    track["sameSlotCollisions"].toArray() == collisions,
            "Incorrect separate-bank collision report");
    require(track["beatgrid"].isArray() && track["beatgrid"].toArray().size() <= 1000000,
            "Invalid beat grid");
    bool first = true;
    double previousIndex = 0;
    double previousFrame = 0;
    for (const auto& value : track["beatgrid"].toArray()) {
        require(value.isObject(), "Invalid beat marker");
        const auto beat = value.toObject();
        require(integer(beat["index"], -9007199254740991.0, 9007199254740991.0) &&
                        number(beat["frame"]),
                "Invalid beat marker coordinates");
        const double index = beat["index"].toDouble();
        const double frame = beat["frame"].toDouble();
        require(first || (index > previousIndex && frame > previousFrame),
                "Non-monotonic beat grid");
        first = false;
        previousIndex = index;
        previousFrame = frame;
    }
}
} // namespace

bool validateEngineImportPackage(const QJsonObject& package, QString* error) {
    if (error) {
        error->clear();
    }
    try {
        require(package["protocol"] == "bitedj.engine.import" &&
                        integer(package["protocolVersion"], 1, 1),
                "Unsupported Engine import protocol");
        require(package["schema"] == "3.0.0" || package["schema"] == "3.0.2",
                "Unsupported Engine schema");
        require(text(package["sourceUuid"], true), "Missing Engine library identity");
        require(package["frameUnit"] == "audio frames at track sample rate",
                "Unsupported frame unit");
        const auto context = package["mediaPathContext"].toObject();
        require(text(context["libraryDirectory"], true) &&
                        context["relativePathBase"] == "original Engine Library directory",
                "Invalid original media context");
        require(package["tracks"].isArray() && package["tracks"].toArray().size() <= 1000000,
                "Invalid track collection");
        QSet<QString> tracks;
        for (const auto& value : package["tracks"].toArray()) {
            require(value.isObject(), "Invalid track object");
            const auto track = value.toObject();
            require(identity(track["id"]) && !tracks.contains(track["id"].toString()),
                    "Invalid or duplicate track identity");
            tracks.insert(track["id"].toString());
            validateTrack(track);
        }
        require(package["playlists"].isArray() && package["playlists"].toArray().size() <= 100000,
                "Invalid playlist collection");
        QHash<QString, QString> parents;
        QHash<QString, QSet<int>> siblingPositions;
        QSet<QString> entries;
        for (const auto& value : package["playlists"].toArray()) {
            require(value.isObject(), "Invalid playlist object");
            const auto playlist = value.toObject();
            const auto id = playlist["id"].toString();
            require(identity(playlist["id"]) && !parents.contains(id), "Duplicate or invalid playlist");
            require(playlist["parentId"].isNull() || identity(playlist["parentId"]),
                    "Invalid playlist parent");
            const auto parent = playlist["parentId"].toString();
            parents.insert(id, parent);
            require(text(playlist["title"]) && integer(playlist["position"], 0, 99999),
                    "Invalid playlist title or position");
            auto& positions = siblingPositions[parent];
            const int position = playlist["position"].toInt();
            require(!positions.contains(position), "Duplicate sibling position");
            positions.insert(position);
            require(playlist["tracks"].isArray(), "Invalid playlist entries");
            for (const auto& entryValue : playlist["tracks"].toArray()) {
                require(entryValue.isObject(), "Invalid playlist entry");
                const auto entry = entryValue.toObject();
                const auto entryId = entry["entryId"].toString();
                require(identity(entry["entryId"]) && !entries.contains(entryId) &&
                                identity(entry["trackId"]) && text(entry["sourceUuid"], true),
                        "Invalid or duplicate playlist entry identity");
                entries.insert(entryId);
                require(entries.size() <= 1000000, "Too many playlist entries");
                const bool local = entry["sourceUuid"] == package["sourceUuid"] &&
                        tracks.contains(entry["trackId"].toString());
                require(entry["resolution"] == (local ? "local" : "unresolved"),
                        "Incorrect playlist reference resolution");
            }
        }
        for (const auto& positions : siblingPositions) {
            for (int i = 0; i < positions.size(); ++i) {
                require(positions.contains(i), "Non-contiguous sibling positions");
            }
        }
        QSet<QString> complete;
        for (auto it = parents.cbegin(); it != parents.cend(); ++it) {
            QString node = it.key();
            QSet<QString> path;
            while (!node.isEmpty() && !complete.contains(node)) {
                require(parents.contains(node) && !path.contains(node), "Missing or cyclic playlist parent");
                path.insert(node);
                node = parents.value(node);
            }
            complete.unite(path);
        }
        return true;
    } catch (const std::invalid_argument& exception) {
        if (error) {
            *error = QString::fromUtf8(exception.what());
        }
        return false;
    }
}
} // namespace mixxx
