#pragma once

#include <QString>
#include <QJsonObject>

namespace mixxx {
// Parent-side protocol validation, before resolving media or mutating the library.
// The caller must bound the helper's bytes/time and reject malformed JSON first.
// Success does not authorize overwriting local cues or validate decoded audio bounds.
bool validateEngineImportPackage(const QJsonObject& package, QString* error);
} // namespace mixxx
