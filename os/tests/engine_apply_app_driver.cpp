// Test-only Qt driver: real menu, reader, Apply button, and library editor.
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <cstdio>

namespace {
int stage = 0, ticks = 0;
QDialog* dialog = nullptr;
void fail(const char* why) {
    std::fprintf(stderr, "APPLY_TEST failed: %s stage=%d\n", why, stage);
    stage = 99; qApp->exit(90);
}
void step() {
    if (stage == 99) return;
    if (++ticks > 400) { fail("timeout"); return; }
    if (stage == 0) {
        for (auto* widget : QApplication::topLevelWidgets()) {
            for (auto* action : widget->findChildren<QAction*>()) {
                if (action->text() != QString::fromUtf8("Preview Engine DJ library…")) continue;
                action->trigger();
                for (auto* window : QApplication::topLevelWidgets())
                    if (window->objectName() == "EngineLibraryPreview") dialog = qobject_cast<QDialog*>(window);
                if (!dialog || !dialog->isVisible()) { fail("dialog not visible"); return; }
                stage = 1;
                if (!QMetaObject::invokeMethod(dialog, "openLibrary", Qt::DirectConnection,
                            Q_ARG(QString, qEnvironmentVariable("BITEDJ_PREVIEW_FIXTURE")))) fail("open slot");
                return;
            }
        }
        return;
    }
    auto* apply = dialog->findChild<QPushButton*>("EngineImportApply");
    auto* status = dialog->findChild<QLabel*>("EnginePreviewStatus");
    auto* details = dialog->findChild<QPlainTextEdit*>("EngineImportDetails");
    if (!apply || !status || !details) { fail("missing import widgets"); return; }
    if (stage == 1) {
        if (!apply->isEnabled()) return;
        if (qEnvironmentVariableIsSet("BITEDJ_APPLY_CANCEL")) {
            bool connected = false;
            for (auto* child : dialog->findChildren<QObject*>()) {
                if (child->inherits("mixxx::EngineImportCoordinator"))
                    connected = bool(QObject::connect(child, SIGNAL(progress(int,int)), dialog,
                            SLOT(reject()), Qt::DirectConnection));
            }
            if (!connected) { fail("cancel connection"); return; }
        }
        if (qEnvironmentVariableIsSet("BITEDJ_APPLY_FAIL_SAVE") || qEnvironmentVariableIsSet("BITEDJ_APPLY_NAME_COLLISION")) {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "apply-failure-fixture");
            db.setDatabaseName(qEnvironmentVariable("BITEDJ_APPLY_DATABASE"));
            if (!db.open()) { fail("failure DB open"); return; }
            QSqlQuery q(db);
            if (qEnvironmentVariableIsSet("BITEDJ_APPLY_NAME_COLLISION") &&
                    !q.exec("INSERT INTO Playlists(name,position,hidden,locked) VALUES('Prepared set',1,0,0)")) { fail("collision fixture"); return; }
            if (qEnvironmentVariableIsSet("BITEDJ_APPLY_FAIL_SAVE") && !q.exec("CREATE TRIGGER fail_import_save BEFORE UPDATE ON library BEGIN SELECT RAISE(ABORT,'injected save failure'); END")) {
                fail("failure trigger"); return;
            }
        }
        stage = 2; apply->click(); return;
    }
    if (stage == 2) {
        if (!status->text().contains("tracks processed")) return;
        std::fprintf(stderr, "APPLY_TEST summary: %s\nAPPLY_TEST details: %s\n",
                status->text().toUtf8().constData(), details->toPlainText().toUtf8().constData());
        if (qEnvironmentVariableIsSet("BITEDJ_APPLY_EDIT_LOCAL")) {
            bool edited = false;
            static int editAttempts = 0;
            ++editAttempts;
            for (auto* widget : QApplication::allWidgets()) {
                auto* table = qobject_cast<QTableView*>(widget);
                if (!table || !table->model()) continue;
                auto* model = table->model();
                int title = -1, comment = -1;
                for (int col = 0; col < model->columnCount(); ++col) {
                    const auto header = model->headerData(col, Qt::Horizontal).toString();
                    if (header == "Title") title = col;
                    if (header == "Comment") comment = col;
                }
                if (editAttempts == 1) std::fprintf(stderr, "APPLY_TEST table %s rows=%d title=%d comment=%d\n", model->metaObject()->className(), model->rowCount(), title, comment);
                if (title < 0 || comment < 0) continue;
                for (int row = 0; row < model->rowCount(); ++row) {
                    if (model->data(model->index(row, title)).toString() == "Signal (Original)")
                        edited = model->setData(model->index(row, title), "My local title", Qt::EditRole);
                }
                if (edited) break;
            }
            if (!edited) {
                if (editAttempts == 1) {
                    for (auto* widget : QApplication::allWidgets()) {
                        auto* tree = qobject_cast<QTreeView*>(widget);
                        if (!tree || !tree->model()) continue;
                        for (int row = 0; row < tree->model()->rowCount(); ++row) {
                            const auto index = tree->model()->index(row, 0);
                            const auto label = tree->model()->data(index).toString();
                            std::fprintf(stderr, "APPLY_TEST tree item: %s\n", label.toUtf8().constData());
                            if (label == "All Tracks") {
                                tree->setCurrentIndex(index);
                                QMetaObject::invokeMethod(tree, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, index));
                            }
                        }
                    }
                }
                if (editAttempts >= 20) fail("ordinary library edit");
                return;
            }
        }
        if (!qEnvironmentVariable("BITEDJ_APPLY_IMAGE").isEmpty() &&
                !dialog->grab().save(qEnvironmentVariable("BITEDJ_APPLY_IMAGE"))) { fail("screenshot"); return; }
        std::fprintf(stderr, "APPLY_TEST PASS\n");
        stage = 99;
        QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
    }
}
void start() {
    qunsetenv("LD_PRELOAD");
    QTimer::singleShot(8000, qApp, [] {
        auto* timer = new QTimer(qApp);
        QObject::connect(timer, &QTimer::timeout, qApp, &step);
        timer->start(100);
    });
}
Q_COREAPP_STARTUP_FUNCTION(start)
} // namespace
