#pragma once
#include <QChar>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <optional>

namespace mixxx {
struct EngineImportKey {
    QString libraryUuid;
    QString kind;     // track, playlist, entry or cue
    QString sourceId; // Opaque within its source library and kind.
};
struct EngineImportRecord {
    std::optional<qint64> localId;
    QJsonObject baseline;
};
// Uses the caller's connection/transaction; never commits independently. Save
// provenance in the same transaction as local entity changes. Missing records
// and SQL/JSON errors are distinct, so read failures cannot trigger a new import.
class EngineImportRegistry {
  public:
    explicit EngineImportRegistry(QSqlDatabase database);
    bool load(const EngineImportKey& key,
            std::optional<EngineImportRecord>* record,
            QString* error) const;
    bool save(const EngineImportKey& key,
            const EngineImportRecord& record,
            QString* error) const;

  private:
    QSqlDatabase m_database;
};
} // namespace mixxx
