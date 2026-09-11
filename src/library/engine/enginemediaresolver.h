#pragma once
#include <QChar>
#include <QJsonObject>
#include <QString>

namespace mixxx {
// Construct from the caller's selected directories, never helper-supplied paths.
// This records a filesystem observation, not a capability to open the path later.
// Revalidate immediately before import/open; it does not establish audio identity.
class EngineMediaResolver {
  public:
    EngineMediaResolver(const QString& libraryDirectory, const QString& mediaRoot);
    QJsonObject resolve(const QString& relativePath) const;
    const QString& libraryDirectory() const {
        return m_library;
    }
    const QString& mediaRoot() const {
        return m_root;
    }

  private:
    QString m_library;
    QString m_root;
};
} // namespace mixxx
