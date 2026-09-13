#include "library/engine/enginemediaresolver.h"

#include <QDir>
#include <QFileInfo>
#include <stdexcept>

namespace mixxx {
namespace {
bool within(const QString& path, const QString& root) {
    return path == root || path.startsWith(root.endsWith('/') ? root : root + '/');
}
} // namespace
EngineMediaResolver::EngineMediaResolver(const QString& libraryDirectory, const QString& mediaRoot)
        : m_library(QFileInfo(libraryDirectory).canonicalFilePath()),
          m_root(QFileInfo(mediaRoot).canonicalFilePath()) {
    if (m_library.isEmpty() || m_root.isEmpty() || !QFileInfo(m_library).isDir() ||
            !QFileInfo(m_root).isDir() || !within(m_library, m_root)) {
        throw std::runtime_error("Engine library must be inside the selected media folder");
    }
}
QJsonObject EngineMediaResolver::resolve(const QString& relativePath) const {
    QJsonObject result{{"relativePath", relativePath}};
    const auto status = [&](const char* value) {
        result.insert("status", QString::fromLatin1(value));
        return result;
    };
    if (relativePath.isEmpty() || relativePath.contains(QChar::Null))
        return status("invalid-reference");
    const auto first = relativePath.front().toLatin1();
    const bool drive = relativePath.size() >= 2 && relativePath[1] == ':' &&
            ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'));
    if (QDir::isAbsolutePath(relativePath) || drive || relativePath.contains('\\'))
        return status("unsupported-reference");
    const QString joined = m_library + '/' + relativePath;
    if (!within(QDir::cleanPath(joined), m_root))
        return status("outside-media-root");
    const QFileInfo file(joined);
    const auto canonical = file.canonicalFilePath();
    if (!canonical.isEmpty() && !within(canonical, m_root))
        return status("outside-media-root");
    if (canonical.isEmpty() || !file.isFile())
        return status("missing");
    if (!file.isReadable())
        return status("unreadable");
    result.insert("path", canonical);
    result.insert("sizeBytes", QString::number(file.size()));
    return status("resolved");
}
} // namespace mixxx
