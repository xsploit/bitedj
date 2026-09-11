// Test-only Qt driver. Exercises the real menu/dialog/reader without replacing
// any import implementation. Required environment: BITEDJ_PREVIEW_FIXTURE,
// BITEDJ_ENGINE_IMPORT_HELPER, BITEDJ_PREVIEW_SLOW_HELPER, BITEDJ_PREVIEW_IMAGE.
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <cstdio>

namespace {
int stage = 0, ticks = 0;
QDialog* dialog = nullptr;
QByteArray originalHelper;
void fail(const char* reason) {
    std::fprintf(stderr, "PREVIEW_TEST failed: %s stage=%d\n", reason, stage);
    stage = 99;
    qApp->exit(90);
}
void open(const QString& folder) {
    if (!QMetaObject::invokeMethod(dialog, "openLibrary", Qt::DirectConnection, Q_ARG(QString, folder)))
        fail("missing openLibrary slot");
}
void step() {
    if (stage == 99)
        return;
    if (++ticks > 400) {
        fail("timeout");
        return;
    }
    const auto fixture = qEnvironmentVariable("BITEDJ_PREVIEW_FIXTURE");
    if (stage == 0) {
        for (auto* widget : QApplication::topLevelWidgets()) {
            for (auto* action : widget->findChildren<QAction*>()) {
                if (action->text() == QString::fromUtf8("Preview Engine DJ library…")) {
                    action->trigger();
                    for (auto* window : QApplication::topLevelWidgets()) {
                        if (window->objectName() == "EngineLibraryPreview")
                            dialog = qobject_cast<QDialog*>(window);
                    }
                    if (!dialog || !dialog->isVisible()) {
                        fail("menu did not show dialog");
                        return;
                    }
                    originalHelper = qgetenv("BITEDJ_ENGINE_IMPORT_HELPER");
                    stage = 1;
                    open(fixture);
                    return;
                }
            }
        }
        return;
    }
    auto* table = dialog->findChild<QTableView*>("EnginePreviewTracks");
    auto* status = dialog->findChild<QLabel*>("EnginePreviewStatus");
    if (!table || !status) {
        fail("missing widgets");
        return;
    }
    static QString previousStatus;
    if (status->text() != previousStatus) {
        previousStatus = status->text();
        std::fprintf(stderr, "PREVIEW_TEST status: %s\n", previousStatus.toUtf8().constData());
    }
    if (stage == 1 || stage == 3) {
        if (table->model()->rowCount() != 2)
            return;
        auto* model = table->model();
        if (model->columnCount() != 6 || !status->text().contains("2 playlists")) {
            fail("summary or columns");
            return;
        }
        QStringList titles, audio;
        for (int row = 0; row < 2; ++row) {
            titles << model->data(model->index(row, 0)).toString();
            audio << model->data(model->index(row, 5)).toString();
            if (model->data(model->index(row, 3)).toInt() != 1 ||
                    model->data(model->index(row, 4)).toInt() != 2) {
                fail("cue/loop counts");
                return;
            }
        }
        if (!titles.contains("Signal (VIP)") || !titles.contains("Signal (Original)") ||
                !audio.contains("Found") || !audio.contains("Missing")) {
            fail("track identities or audio status");
            return;
        }
        if (stage == 3) {
            if (!dialog->grab().save(qEnvironmentVariable("BITEDJ_PREVIEW_IMAGE"))) {
                fail("screenshot");
                return;
            }
            std::fprintf(stderr, "PREVIEW_TEST PASS real menu/reader, Original/VIP, audio status, invalid folder, missing helper, dialog cancellation and reopen\n");
            stage = 99;
            qApp->quit();
            return;
        }
        open(fixture + "/absent");
        if (model->rowCount() != 0 || !status->text().startsWith("No Engine library")) {
            fail("stale preview after invalid selection");
            return;
        }
        qputenv("BITEDJ_ENGINE_IMPORT_HELPER", "/nonexistent/bitedj-test-helper");
        open(fixture);
        if (!status->text().contains("not installed")) {
            fail("missing helper");
            return;
        }
        qputenv("BITEDJ_ENGINE_IMPORT_HELPER", qgetenv("BITEDJ_PREVIEW_SLOW_HELPER"));
        open(fixture);
        dialog->reject();
        stage = 2;
    } else if (stage == 2 && status->text().startsWith("Reading cancelled")) {
        qputenv("BITEDJ_ENGINE_IMPORT_HELPER", originalHelper);
        dialog->show();
        stage = 3;
        open(fixture + "/Engine Library");
    }
}
void start() {
    // Keep this UI driver out of the real Python/native reader subprocesses.
    qunsetenv("LD_PRELOAD");
    QTimer::singleShot(8000, qApp, [] {
        auto* timer = new QTimer(qApp);
        QObject::connect(timer, &QTimer::timeout, qApp, &step);
        timer->start(100);
    });
}
Q_COREAPP_STARTUP_FUNCTION(start)
} // namespace
