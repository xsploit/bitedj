#include "library/engine/enginereaderservice.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QStringConverter>
#include <QThread>
#include <QVector>
#include <stdexcept>

#include "library/engine/engineimportpackage.h"
#include "moc_enginereaderservice.cpp"
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace mixxx {
namespace {
constexpr qint64 kOutputLimit = 64 * 1024 * 1024;
constexpr qint64 kErrorLimit = 1024 * 1024;
constexpr qint64 kTimeoutMs = 150000;
struct Cancelled {};
void checkCancel(const std::atomic_bool& cancel) {
    if (cancel.load()) {
        throw Cancelled{};
    }
}
void stop(QProcess& process, [[maybe_unused]] qint64 groupId) {
    // Let Python unwind its snapshot and reap its native reader first.
    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        process.waitForFinished(500);
    }
#ifdef Q_OS_UNIX
    // Also stop any remaining descendants in our dedicated process group.

    if (groupId > 0) {
        ::kill(-groupId, SIGKILL);
    }
#endif
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
}
// Qt accepts some non-JSON escapes, controls and numeric spellings. Enforce
// lexical JSON grammar before relying on its structural parser.
void checkJsonTokens(const QByteArray& bytes, const std::atomic_bool& cancel) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    for (qsizetype offset = 0; offset < bytes.size(); offset += 65536) {
        checkCancel(cancel);
        const QString decoded = decoder(QByteArrayView(bytes).sliced(offset, qMin<qsizetype>(65536, bytes.size() - offset)));
        Q_UNUSED(decoded);
        if (decoder.hasError()) {
            throw std::runtime_error("Invalid UTF-8 in Engine reader output");
        }
    }
    auto bad = [] { throw std::runtime_error("Invalid JSON token from Engine reader"); };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto delimiter = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ']' || c == '}';
    };
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        checkCancel(cancel);
        const char c = bytes[i];
        if (c == '"') {
            bool closed = false;
            while (++i < bytes.size()) {
                const unsigned char value = bytes[i];
                if (value < 0x20)
                    bad();
                if (value == '"') {
                    closed = true;
                    break;
                }
                if (value != '\\')
                    continue;
                if (++i >= bytes.size())
                    bad();
                const char escaped = bytes[i];
                if (escaped == 'u') {
                    for (int h = 0; h < 4; ++h) {
                        if (++i >= bytes.size())
                            bad();
                        const char hex = bytes[i];
                        if (!digit(hex) && !(hex >= 'a' && hex <= 'f') && !(hex >= 'A' && hex <= 'F'))
                            bad();
                    }
                } else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' &&
                        escaped != 'f' && escaped != 'n' && escaped != 'r' && escaped != 't') {
                    bad();
                }
            }
            if (!closed)
                bad();
        } else if (c == '-' || digit(c)) {
            auto end = i;
            if (bytes[end] == '-')
                ++end;
            if (end >= bytes.size())
                bad();
            if (bytes[end] == '0')
                ++end;
            else {
                if (bytes[end] < '1' || bytes[end] > '9')
                    bad();
                while (end < bytes.size() && digit(bytes[end]))
                    ++end;
            }
            if (end < bytes.size() && bytes[end] == '.') {
                ++end;
                if (end >= bytes.size() || !digit(bytes[end]))
                    bad();
                while (end < bytes.size() && digit(bytes[end]))
                    ++end;
            }
            if (end < bytes.size() && (bytes[end] == 'e' || bytes[end] == 'E')) {
                ++end;
                if (end < bytes.size() && (bytes[end] == '+' || bytes[end] == '-'))
                    ++end;
                if (end >= bytes.size() || !digit(bytes[end]))
                    bad();
                while (end < bytes.size() && digit(bytes[end]))
                    ++end;
            }
            if (end < bytes.size() && !delimiter(bytes[end]))
                bad();
            i = end - 1;
        } else if (c == 't' || c == 'f' || c == 'n') {
            const QByteArray token = c == 't' ? "true" : c == 'f' ? "false"
                                                                  : "null";
            if (bytes.mid(i, token.size()) != token)
                bad();
            i += token.size() - 1;
            if (i + 1 < bytes.size() && !delimiter(bytes[i + 1]))
                bad();
        } else if (c != '{' && c != '}' && c != '[' && c != ']' && c != ':' && c != ',' &&
                c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            bad();
        }
    }
}

// Qt's JSON parser accepts duplicate object members. After syntax validation,
// scan object keys and reject duplicates, including equivalent escaped keys.
// The scan is iterative and cancellable; arrays/objects remain whole values.
void checkDuplicateKeys(const QByteArray& bytes, const std::atomic_bool& cancel) {
    struct Frame {
        bool object;
        bool key;
        QSet<QString> keys;
    };
    QVector<Frame> stack;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if ((i & 4095) == 0) {
            checkCancel(cancel);
        }
        const char ch = bytes[i];
        if (ch == '{' || ch == '[') {
            stack.append({ch == '{', ch == '{', {}});
        } else if (ch == '}' || ch == ']') {
            stack.removeLast();
        } else if (ch == ',') {
            if (!stack.isEmpty() && stack.last().object) {
                stack.last().key = true;
            }
        } else if (ch == '"') {
            const auto start = i;
            for (++i; i < bytes.size(); ++i) {
                if (bytes[i] == '\\') {
                    ++i;
                } else if (bytes[i] == '"') {
                    break;
                }
            }
            if (!stack.isEmpty() && stack.last().object && stack.last().key) {
                auto& frame = stack.last();
                const auto keyJson = QByteArray("[") + bytes.mid(start, i - start + 1) + ']';
                const auto key = QJsonDocument::fromJson(keyJson).array().at(0).toString();
                if (frame.keys.contains(key)) {
                    throw std::runtime_error("Duplicate object member in Engine reader output");
                }
                frame.keys.insert(key);
                frame.key = false;
            }
        }
    }
}
QJsonObject readPackage(const QString& launcher,
        const QString& library,
        const QString& mediaRoot,
        const std::atomic_bool& cancel) {
    checkCancel(cancel);
    const QFileInfo executable(launcher);
    if (!executable.isAbsolute() || !executable.isFile() || !executable.isExecutable()) {
        throw std::runtime_error("Engine reader launcher must be an executable absolute path");
    }
    if (!QFileInfo(library).isDir() || !QFileInfo(mediaRoot).isDir()) {
        throw std::runtime_error("Engine library and media root must be existing directories");
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_UNIX
    process.setChildProcessModifier([] {
        if (::setsid() < 0) {
            ::_exit(127);
        }
    });
#endif
    qint64 processGroup = 0;
    QObject::connect(&process, &QProcess::started, &process, [&] {
        processGroup = process.processId();
    });
    QElapsedTimer elapsed;
    elapsed.start();
    process.start(executable.canonicalFilePath(),
            {QFileInfo(library).absoluteFilePath(), "--media-root", QFileInfo(mediaRoot).absoluteFilePath()});
    QByteArray output;
    QByteArray diagnostic;
    try {
        while (process.state() == QProcess::Starting) {
            checkCancel(cancel);
            process.waitForStarted(50);
            if (elapsed.elapsed() > kTimeoutMs) {
                throw std::runtime_error("Engine reader startup timed out");
            }
        }
        if (process.error() == QProcess::FailedToStart) {
            throw std::runtime_error(process.errorString().toStdString());
        }
        do {
            checkCancel(cancel);
            process.setReadChannel(QProcess::StandardOutput);
            while (process.bytesAvailable() > 0) {
                checkCancel(cancel);
                auto chunk = process.read(65536);
                if (output.size() + chunk.size() > kOutputLimit) {
                    throw std::runtime_error("Engine reader output exceeds 64 MiB");
                }
                output.append(chunk);
            }
            process.setReadChannel(QProcess::StandardError);
            while (process.bytesAvailable() > 0) {
                auto chunk = process.read(65536);
                if (diagnostic.size() + chunk.size() > kErrorLimit) {
                    throw std::runtime_error("Engine reader diagnostics exceed 1 MiB");
                }
                diagnostic.append(chunk);
            }
            if (elapsed.elapsed() > kTimeoutMs) {
                throw std::runtime_error("Engine reader timed out");
            }
            if (process.state() == QProcess::NotRunning) {
                break;
            }
            process.waitForFinished(50);
        } while (true);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            throw std::runtime_error(QString("Engine reader failed (%1): %2")
                            .arg(process.exitCode())
                            .arg(QString::fromUtf8(diagnostic.left(4096)))
                            .toStdString());
        }
        checkCancel(cancel);
        checkJsonTokens(output, cancel);
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(output, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            throw std::runtime_error("Invalid JSON from Engine reader");
        }
        checkDuplicateKeys(output, cancel);
        const auto package = document.object();
        QString error;
        if (!validateEngineImportPackage(package, &error)) {
            throw std::runtime_error(error.toStdString());
        }
        checkCancel(cancel);
        stop(process, processGroup);
        return package;
    } catch (...) {
        stop(process, processGroup);
        throw;
    }
}
} // namespace
EngineReaderService::EngineReaderService(QObject* parent)
        : QObject(parent) {
}
EngineReaderService::~EngineReaderService() {
    cancel();
    if (m_worker) {
        m_worker->wait();
    }
}
bool EngineReaderService::isRunning() const {
    return m_busy;
}
void EngineReaderService::cancel() {
    if (m_cancel) {
        m_cancel->store(true);
    }
}
bool EngineReaderService::start(const QString& launcher, const QString& library, const QString& mediaRoot) {
    if (isRunning()) {
        return false;
    }
    m_busy = true;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_worker.reset(QThread::create([this, launcher, library, mediaRoot, cancel = m_cancel] {
        QJsonObject package;
        QString error;
        bool wasCancelled = false;
        try {
            package = readPackage(launcher, library, mediaRoot, *cancel);
        } catch (const Cancelled&) {
            wasCancelled = true;
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        }
        // Keep the request busy until its result reaches the owner thread. This
        // prevents queued output from an earlier request overtaking a new one.
        QMetaObject::invokeMethod(this, [this, package, error, wasCancelled, cancel] {
            m_worker->wait();
            m_busy = false;
            if (wasCancelled || cancel->load()) {
                emit cancelled();
            } else if (!error.isEmpty()) {
                emit failed(error);
            } else {
                emit packageReady(package);
            } }, Qt::QueuedConnection);
    }));
    m_worker->start();
    return true;
}
} // namespace mixxx
