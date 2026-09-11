#include "library/dao/cuedao.h"

#include <QMutexLocker>
#include <QSet>
#include <QSqlError>
#include <QThread>
#include <QVariant>
#include <QtDebug>

#include "engine/engine.h"
#include "library/queryutil.h"
#include "util/assert.h"
#include "util/color/rgbcolor.h"
#include "util/db/fwdsqlquery.h"
#include "util/db/sqltransaction.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger = mixxx::Logger("CueDAO");

/// Wrap a `QString` label in a `QVariant`. The label column is not nullable,
/// so this function also makes sure that the label an empty string, not null.
inline const QVariant labelToQVariant(const QString& label) {
    if (label.isNull()) {
        return QLatin1String(""); // null -> empty
    } else {
        return label;
    }
}

/// Empty labels are read as null strings
inline QString labelFromQVariant(const QVariant& value) {
    const auto label = value.toString();
    if (label.isEmpty()) {
        return QString(); // empty -> null
    } else {
        return label;
    }
}

CuePointer cueFromRow(const QSqlRecord& row) {
    const auto id = DbId(row.value(row.indexOf("id")));
    TrackId trackId(row.value(row.indexOf("track_id")));
    auto type = static_cast<mixxx::CueType>(row.value(row.indexOf("type")).toInt());
    const auto position =
            mixxx::audio::FramePos::fromEngineSamplePosMaybeInvalid(
                    row.value(row.indexOf("position")).toDouble());
    double lengthFrames = row.value(row.indexOf("length")).toDouble() / mixxx::kEngineChannelCount;
    int hotcue = row.value(row.indexOf("hotcue")).toInt();
    QString label = labelFromQVariant(row.value(row.indexOf("label")));
    mixxx::RgbColor::optional_t color = mixxx::RgbColor::fromQVariant(row.value(row.indexOf("color")));
    VERIFY_OR_DEBUG_ASSERT(color) {
        return CuePointer();
    }
    if (type == mixxx::CueType::Loop && lengthFrames == 0.0) {
        // These entries are likely added via issue #11283
        qWarning() << "Discard loop cue" << hotcue << "found in database with length of 0";
        return CuePointer();
    }
    if (type == mixxx::CueType::HotCue &&
            position == mixxx::audio::FramePos(0) &&
            *color == mixxx::RgbColor(0)) {
        // These entries are likely added via issue #11283
        qWarning() << "Discard black hot cue" << hotcue << "found in database at position 0";
        return CuePointer();
    }
    std::optional<Cue::EngineOrigin> origin;
    const auto sourceUuid = row.value(row.indexOf("engine_library_uuid")).toString();
    const auto sourceTrack = row.value(row.indexOf("engine_track_id")).toString();
    const int sourceBank = row.value(row.indexOf("engine_bank")).toInt();
    const int sourceSlot = row.value(row.indexOf("engine_slot")).toInt();
    if (!sourceUuid.isEmpty() && !sourceTrack.isEmpty() &&
            (sourceBank == 1 || sourceBank == 2) && sourceSlot >= 1 && sourceSlot <= 8) {
        origin = Cue::EngineOrigin{sourceUuid, sourceTrack,
                static_cast<Cue::EngineOrigin::Bank>(sourceBank), sourceSlot};
    }
    CuePointer pCue(new Cue(id,
            type,
            position,
            lengthFrames,
            hotcue,
            label,
            *color,
            std::move(origin)));
    return pCue;
}

} // namespace

QList<CuePointer> CueDAO::getCuesForTrack(TrackId trackId) const {
    //qDebug() << "CueDAO::getCuesForTrack" << QThread::currentThread() << m_database.connectionName();
    QList<CuePointer> cues;

    FwdSqlQuery query(
            m_database,
            QStringLiteral("SELECT * FROM " CUE_TABLE " WHERE track_id=:id"));
    DEBUG_ASSERT(
            query.isPrepared() &&
            !query.hasError());
    query.bindValue(":id", trackId);
    if (!query.execPrepared()) {
        kLogger.warning()
                << "Failed to load cues of track"
                << trackId;
        DEBUG_ASSERT(!"failed query");
        return cues;
    }
    QMap<int, CuePointer> hotCuesByNumber;
    while (query.next()) {
        CuePointer pCue = cueFromRow(query.record());
        if (!pCue) {
            continue;
        }
        int hotCueNumber = pCue->getHotCue();
        if (hotCueNumber != Cue::kNoHotCue) {
            const auto pDuplicateCue = hotCuesByNumber.take(hotCueNumber);
            if (pDuplicateCue) {
                kLogger.warning()
                        << "Dropping hot cue"
                        << pDuplicateCue->getId()
                        << "with duplicate number"
                        << hotCueNumber;
                cues.removeOne(pDuplicateCue);
            }
            hotCuesByNumber.insert(hotCueNumber, pCue);
        }
        cues.push_back(pCue);
    }
    return cues;
}

bool CueDAO::deleteCuesForTrack(TrackId trackId) const {
    qDebug() << "CueDAO::deleteCuesForTrack" << QThread::currentThread() << m_database.connectionName();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM " CUE_TABLE " WHERE track_id=:track_id"));
    query.bindValue(":track_id", trackId.toVariant());
    if (query.exec()) {
        return true;
    } else {
        LOG_FAILED_QUERY(query);
    }
    return false;
}

bool CueDAO::deleteCuesForTracks(const QList<TrackId>& trackIds) const {
    qDebug() << "CueDAO::deleteCuesForTracks" << QThread::currentThread() << m_database.connectionName();

    QStringList idList;
    for (const auto& trackId: trackIds) {
        idList << trackId.toString();
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM " CUE_TABLE " WHERE track_id in (%1)")
                  .arg(idList.join(",")));
    if (query.exec()) {
        return true;
    } else {
        LOG_FAILED_QUERY(query);
    }
    return false;
}

bool CueDAO::saveCue(TrackId trackId, Cue* cue) const {
    //qDebug() << "CueDAO::saveCue" << QThread::currentThread() << m_database.connectionName();
    VERIFY_OR_DEBUG_ASSERT(cue) {
        return false;
    }

    // Prepare query
    QSqlQuery query(m_database);
    if (cue->getId().isValid()) {
        // Update cue
        query.prepare(QStringLiteral("UPDATE " CUE_TABLE " SET "
                        "track_id=:track_id,"
                        "type=:type,"
                        "position=:position,"
                        "length=:length,"
                        "hotcue=:hotcue,"
                        "label=:label,"
                        "color=:color,"
                        "engine_library_uuid=:engine_library_uuid,"
                        "engine_track_id=:engine_track_id,"
                        "engine_bank=:engine_bank,"
                        "engine_slot=:engine_slot"
                        " WHERE id=:id"));
        query.bindValue(":id", cue->getId().toVariant());
    } else {
        // New cue
        query.prepare(
                QStringLiteral("INSERT INTO " CUE_TABLE
                               " (track_id, type, position, length, hotcue, "
                               "label, color, engine_library_uuid, engine_track_id, engine_bank, engine_slot) "
                               "VALUES (:track_id, :type, :position, :length, :hotcue, :label, :color, "
                               ":engine_library_uuid, :engine_track_id, :engine_bank, :engine_slot)"));
    }

    // Bind values and execute query
    query.bindValue(":track_id", trackId.toVariant());
    query.bindValue(":type", static_cast<int>(cue->getType()));
    query.bindValue(":position", cue->getPosition().toEngineSamplePosMaybeInvalid());
    query.bindValue(":length", cue->getLengthFrames() * mixxx::kEngineChannelCount);
    query.bindValue(":hotcue", cue->getHotCue());
    query.bindValue(":label", labelToQVariant(cue->getLabel()));
    query.bindValue(":color", mixxx::RgbColor::toQVariant(cue->getColor()));
    const auto origin = cue->getEngineOrigin();
    query.bindValue(":engine_library_uuid", origin ? QVariant(origin->libraryUuid) : QVariant());
    query.bindValue(":engine_track_id", origin ? QVariant(origin->trackId) : QVariant());
    query.bindValue(":engine_bank", origin ? QVariant(static_cast<int>(origin->bank)) : QVariant());
    query.bindValue(":engine_slot", origin ? QVariant(origin->slot) : QVariant());
    if (!query.exec() || query.numRowsAffected() != 1) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    if (!cue->getId().isValid()) {
        // New cue
        const auto newId = DbId(query.lastInsertId());
        DEBUG_ASSERT(newId.isValid());
        cue->setId(newId);
    }
    DEBUG_ASSERT(cue->getId().isValid());
    cue->setDirty(false);
    return true;
}

void CueDAO::saveTrackCues(
        TrackId trackId,
        const QList<CuePointer>& cueList) const {
    DEBUG_ASSERT(trackId.isValid());
    QStringList cueIds;
    cueIds.reserve(cueList.size());
    for (const auto& pCue : cueList) {
        // New cues (without an id) must always be marked as dirty
        DEBUG_ASSERT(pCue->getId().isValid() || pCue->isDirty());
        // Update or save cue
        if (pCue->isDirty()) {
            saveCue(trackId, pCue.get());
        }
        // After saving each cue must have a valid id
        VERIFY_OR_DEBUG_ASSERT(pCue->getId().isValid()) {
            continue;
        }
        cueIds.append(pCue->getId().toString());
    }

    // Delete orphaned cues
    FwdSqlQuery query(
            m_database,
            QStringLiteral("DELETE FROM " CUE_TABLE " WHERE track_id=:track_id AND id NOT IN (%1)")
                    .arg(cueIds.join(QChar(','))));
    DEBUG_ASSERT(
            query.isPrepared() &&
            !query.hasError());
    query.bindValue(":track_id", trackId);
    if (!query.execPrepared()) {
        kLogger.warning()
                << "Failed to delete orphaned cues of track"
                << trackId;
        DEBUG_ASSERT(!"failed query");
        return;
    }
    if (query.numRowsAffected() > 0) {
        kLogger.debug()
                << "Deleted"
                << query.numRowsAffected()
                << "orphaned cue(s) of track"
                << trackId;
    }
}

bool CueDAO::stageTrackCues(const SqlTransaction& transaction,
        TrackId trackId,
        const QList<CuePointer>& cues,
        QList<CuePointer>* staged,
        QString* error) const {
    if (error)
        error->clear();
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!staged)
        return fail("Missing staged cue output");
    const auto inputCues = cues; // The output container may alias the input.
    staged->clear();
    if (!transaction || transaction.database().connectionName() != m_database.connectionName() || !trackId.isValid()) {
        return fail("Cue staging requires an active transaction on this connection and a valid track ID");
    }
    QList<CuePointer> copies;
    QSet<int> controlIndices;
    QSet<QString> existingIds;
    for (const auto& cue : inputCues) {
        if (!cue)
            return fail("Null cue in staged cue list");
        // Capture each cue consistently under its own mutex. This is a detached
        // snapshot; callers must separately check that live data did not change
        // between planning and postcommit publication.
        QMutexLocker lock(&cue->m_mutex);
        CuePointer copy(new Cue(cue->m_type, cue->m_iHotCue, cue->m_startPosition, cue->m_endPosition, cue->m_color));
        copy->m_dbId = cue->m_dbId;
        copy->m_label = cue->m_label;
        copy->m_engineOrigin = cue->m_engineOrigin;
        copy->m_bDirty = true;
        if (copy->m_iHotCue != Cue::kNoHotCue) {
            if (controlIndices.contains(copy->m_iHotCue))
                return fail("Duplicate local cue control index");
            controlIndices.insert(copy->m_iHotCue);
        }
        if (copy->m_dbId.isValid()) {
            const auto id = copy->m_dbId.toString();
            if (existingIds.contains(id))
                return fail("Duplicate cue database identity");
            existingIds.insert(id);
        }
        copies.append(std::move(copy));
    }
    QSqlQuery query(m_database);
    if (!query.exec("SAVEPOINT bitedj_stage_cues"))
        return fail(query.lastError().text());
    auto rollback = [&](const QString& message) {
        QSqlQuery undo(m_database);
        const bool rolledBack = undo.exec("ROLLBACK TO SAVEPOINT bitedj_stage_cues");
        const bool released = undo.exec("RELEASE SAVEPOINT bitedj_stage_cues");
        return fail(message + ((!rolledBack || !released) ? "; discard the outer transaction: savepoint cleanup failed" : ""));
    };
    QStringList ids;
    for (const auto& copy : copies) {
        if (copy->getId().isValid()) {
            if (!query.prepare("SELECT track_id FROM cues WHERE id=:id"))
                return rollback(query.lastError().text());
            query.bindValue(":id", copy->getId().toVariant());
            if (!query.exec())
                return rollback(query.lastError().text());
            if (!query.next() || TrackId(query.value(0)) != trackId)
                return rollback("Stale cue identity or cue belongs to another track");
            query.finish();
        }
        if (!saveCue(trackId, copy.get()))
            return rollback("Failed to write staged cue");
        ids.append(copy->getId().toString());
    }
    if (!query.prepare(QString("DELETE FROM cues WHERE track_id=:track_id AND id NOT IN (%1)").arg(ids.join(','))))
        return rollback(query.lastError().text());
    query.bindValue(":track_id", trackId.toVariant());
    if (!query.exec())
        return rollback(query.lastError().text());
    if (!query.exec("RELEASE SAVEPOINT bitedj_stage_cues"))
        return rollback(query.lastError().text());
    *staged = std::move(copies);
    return true;
}
