#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QThread>
#include <QTimer>
#include <iostream>

#include "library/engine/enginereaderservice.h"
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 4)
        return 2;
    mixxx::EngineReaderService service;
    const QString mode(argv[3]);
    const bool queuedCancel = mode == "cancel-queued";
    const bool twice = mode == "ready-twice";
    const bool media = mode == "ready-media";
    const QString expected = queuedCancel ? "cancelled" : (twice || media) ? "ready" : mode;
    int ticks = 0, terminals = 0, result = 1;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
    heartbeat.start(10);
    auto done = [&](const QString& actual) {
        ++terminals;
        result = actual.startsWith(expected) ? 0 : 1;
        if (result)
            std::cerr << "Unexpected: " << actual.toStdString() << '\n';
        if (!result && twice && terminals == 1) {
            if (!service.start(argv[1], argv[2], argv[2])) {
                result = 7;
                app.quit();
            }
            return;
        }
        QTimer::singleShot(20, &app, &QCoreApplication::quit);
    };
    QObject::connect(&service, &mixxx::EngineReaderService::packageReady, &app, [&](const QJsonObject& p) {
        if (media) {
            const auto tracks = p["tracks"].toArray();
            if (tracks.size() != 2 ||
                    tracks[0].toObject()["media"].toObject()["path"] != QFileInfo(QString(argv[2]) + "/a.wav").canonicalFilePath() ||
                    tracks[0].toObject()["media"].toObject()["sizeBytes"] != "7" ||
                    tracks[1].toObject()["media"].toObject()["status"] != "outside-media-root" ||
                    tracks[1].toObject()["media"].toObject().contains("path") ||
                    p["receivedLibraryArgument"] != QFileInfo(argv[2]).canonicalFilePath() ||
                    p["receivedMediaRoot"] != QFileInfo(argv[2]).canonicalFilePath() ||
                    p["mediaPathContext"].toObject()["libraryDirectory"] != QFileInfo(argv[2]).canonicalFilePath()) {
                done("wrong parent media resolution");
                return;
            }
        }
        if (p["sourceUuid"].toString().isEmpty())
            done("wrong package");
        else
            done("ready");
    });
    QObject::connect(&service, &mixxx::EngineReaderService::failed, &app, [&](const QString& message) { done("failed: " + message); });
    QObject::connect(&service, &mixxx::EngineReaderService::cancelled, &app, [&] { done("cancelled"); });
    if (!service.start(argv[1], argv[2], argv[2]))
        return 3;
    if (service.start(argv[1], argv[2], argv[2]))
        return 4;
    if (expected == "cancelled")
        QTimer::singleShot(150, &service, &mixxx::EngineReaderService::cancel);
    QTimer::singleShot(10000, &app, [&] {std::cerr << "Timeout\n";result=5;app.quit(); });
    if (queuedCancel) {
        QThread::msleep(500);
        if (!service.isRunning() || service.start(argv[1], argv[2], argv[2])) return 8;
        service.cancel();
    }
    app.exec();
    if (terminals != (twice ? 2 : 1) || (expected == "ready" && ticks < 3))
        return 6;
    std::cout << "terminals=" << terminals << " GUI ticks=" << ticks << '\n';
    return result;
}
