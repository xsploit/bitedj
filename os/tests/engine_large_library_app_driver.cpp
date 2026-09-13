// Test-only real menu/reader/Apply driver. No importer implementation is replaced.
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <algorithm>
#include <cstdio>
namespace {
QDialog* dialog = nullptr;
QElapsedTimer clock;
qint64 previous = 0, phaseStart = 0, maxReadingGap = 0, maxApplyGap = 0;
qint64 readingMs = 0;
int stage = 0;
void fail(const char* reason) {
    std::fprintf(stderr, "LARGE_TEST FAIL %s stage=%d\n", reason, stage);
    stage = 99; qApp->exit(90);
}
void step() {
    if (stage == 99) return;
    const auto now = clock.elapsed();
    const auto gap = now - previous; previous = now;
    if (stage == 1) maxReadingGap = std::max(maxReadingGap, gap);
    if (stage == 2) maxApplyGap = std::max(maxApplyGap, gap);
    if (now > 240000) { fail("timeout"); return; }
    if (stage == 0) {
        for (auto* widget : QApplication::topLevelWidgets()) {
            for (auto* action : widget->findChildren<QAction*>()) {
                if (action->text() != QString::fromUtf8("Preview Engine DJ library…")) continue;
                action->trigger();
                for (auto* window : QApplication::topLevelWidgets())
                    if (window->objectName() == "EngineLibraryPreview") dialog = qobject_cast<QDialog*>(window);
                if (!dialog) { fail("dialog"); return; }
                stage = 1; phaseStart = now;
                if (!QMetaObject::invokeMethod(dialog, "openLibrary", Qt::DirectConnection,
                            Q_ARG(QString, qEnvironmentVariable("BITEDJ_LARGE_DRIVE")))) fail("openLibrary");
                return;
            }
        }
        return;
    }
    auto* apply = dialog->findChild<QPushButton*>("EngineImportApply");
    auto* status = dialog->findChild<QLabel*>("EnginePreviewStatus");
    auto* table = dialog->findChild<QTableView*>("EnginePreviewTracks");
    auto* details = dialog->findChild<QPlainTextEdit*>("EngineImportDetails");
    if (!apply || !status || !table || !details) { fail("widgets"); return; }
    if (stage == 1) {
        if (!apply->isEnabled()) return;
        if (table->model()->rowCount() != qEnvironmentVariableIntValue("BITEDJ_LARGE_COUNT")) { fail("preview count"); return; }
        readingMs = now - phaseStart; phaseStart = now; stage = 2;
        apply->click(); return;
    }
    if (!status->text().contains("tracks processed")) return;
    QJsonObject result{{"readingMs", readingMs}, {"applyMs", now - phaseStart},
        {"maxReadingHeartbeatGapMs", maxReadingGap}, {"maxApplyHeartbeatGapMs", maxApplyGap},
        {"requestedHeartbeatMs", 10}, {"summary", status->text()}, {"details", details->toPlainText()}};
    QFile file(qEnvironmentVariable("BITEDJ_LARGE_RESULT"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson()) <= 0) { fail("result"); return; }
    file.close();
    std::fprintf(stderr, "LARGE_TEST PASS %s\n", status->text().toUtf8().constData());
    stage = 99; qApp->quit();
}
void start() {
    qunsetenv("LD_PRELOAD");
    QTimer::singleShot(8000, qApp, [] {
        clock.start(); previous = 0;
        auto* timer = new QTimer(qApp); timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(timer, &QTimer::timeout, qApp, &step); timer->start(10);
    });
}
Q_COREAPP_STARTUP_FUNCTION(start)
}
