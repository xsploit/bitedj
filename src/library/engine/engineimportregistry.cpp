#include "library/engine/engineimportregistry.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <utility>

namespace mixxx {
namespace {
bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}
bool valid(const EngineImportKey& key) {
    for (const auto& field : {key.libraryUuid, key.kind, key.sourceId}) {
        if (field.isEmpty() || field.size() > 65536 || field.contains(QChar(0))) {
            return false;
        }
    }
    return key.kind == "track" || key.kind == "playlist" ||
            key.kind == "entry" || key.kind == "cue";
}
void bindKey(QSqlQuery* query, const EngineImportKey& key) {
    query->bindValue(":library", key.libraryUuid);
    query->bindValue(":kind", key.kind);
    query->bindValue(":source", key.sourceId);
}
} // namespace
EngineImportRegistry::EngineImportRegistry(QSqlDatabase database)
        : m_database(std::move(database)) {
}
bool EngineImportRegistry::load(const EngineImportKey& key,
        std::optional<EngineImportRecord>* record,
        QString* error) const {
    if (error) {
        error->clear();
    }
    if (!record) {
        return fail(error, "Missing import record output");
    }
    record->reset();
    if (!valid(key)) {
        return fail(error, "Invalid Engine source identity");
    }
    QSqlQuery query(m_database);
    if (!query.prepare("SELECT local_id, source_baseline FROM engine_import_entities "
                       "WHERE library_uuid=:library AND entity_kind=:kind AND source_id=:source")) {
        return fail(error, query.lastError().text());
    }
    bindKey(&query, key);
    if (!query.exec()) {
        return fail(error, query.lastError().text());
    }
    if (!query.next()) {
        return query.lastError().isValid() ? fail(error, query.lastError().text()) : true;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(query.value(1).toByteArray(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(error, "Invalid stored Engine import baseline");
    }
    std::optional<qint64> localId;
    if (!query.value(0).isNull()) {
        bool ok;
        const auto id = query.value(0).toLongLong(&ok);
        if (!ok || id <= 0) {
            return fail(error, "Invalid stored local entity identity");
        }
        localId = id;
    }
    *record = EngineImportRecord{localId, document.object()};
    return true;
}
bool EngineImportRegistry::save(const EngineImportKey& key,
        const EngineImportRecord& record,
        QString* error) const {
    if (error) {
        error->clear();
    }
    if (!valid(key) || (record.localId && *record.localId <= 0)) {
        return fail(error, "Invalid Engine import identity");
    }
    QSqlQuery query(m_database);
    if (!query.prepare("INSERT OR REPLACE INTO engine_import_entities "
                       "(library_uuid, entity_kind, source_id, local_id, source_baseline) "
                       "VALUES (:library,:kind,:source,:local,:baseline)")) {
        return fail(error, query.lastError().text());
    }
    bindKey(&query, key);
    query.bindValue(":local", record.localId ? QVariant(*record.localId) : QVariant());
    query.bindValue(":baseline", QJsonDocument(record.baseline).toJson(QJsonDocument::Compact));
    return query.exec() ? true : fail(error, query.lastError().text());
}
} // namespace mixxx
