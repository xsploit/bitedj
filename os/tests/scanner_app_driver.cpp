#include <QAction>
#include <QApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QWidget>
#include <cstdio>
namespace {
QAction* action = nullptr;
int stage = 0, ticks = 0, waitTicks = 0;
void fail(const char* msg) {
    fprintf(stderr, "SCAN_TEST FAIL %s\n", msg);
    stage = 99;
    qApp->exit(90);
}
void step() {
    if (stage == 99)
        return;
    if (++ticks > 300) {
        fail("timeout");
        return;
    }
    if (stage == 0) {
        for (auto* w : QApplication::topLevelWidgets())
            for (auto* a : w->findChildren<QAction*>())
                if (a->text().remove('&') == "Rescan Library")
                    action = a;
        if (!action || !action->isEnabled())
            return;
        auto db = QSqlDatabase::addDatabase("QSQLITE", "scan-fixture");
        db.setDatabaseName(qEnvironmentVariable("SCAN_DB"));
        if (!db.open()) {
            fail("db");
            return;
        }
        QSqlQuery q(db);
        q.prepare("INSERT INTO directories(directory) VALUES(?)");
        q.addBindValue(qEnvironmentVariable("SCAN_MUSIC"));
        if (!q.exec()) {
            fail("directory");
            return;
        }
        if (qEnvironmentVariableIsSet("SCAN_REJECT") && !q.exec("CREATE TRIGGER reject_scan BEFORE INSERT ON library BEGIN SELECT RAISE(FAIL,'synthetic scan failure'); END")) {
            fail("trigger");
            return;
        }
        stage = 1;
        action->trigger();
        return;
    }
    if (++waitTicks < 25 || !action->isEnabled())
        return;
    QSqlQuery q(QSqlDatabase::database("scan-fixture"));
    if (!q.exec("SELECT count(*) FROM library") || !q.next()) {
        fail("count query");
        return;
    }
    const int expected = qEnvironmentVariableIsSet("SCAN_REJECT") ? 0 : 3;
    if (q.value(0).toInt() != expected) {
        fail("track count");
        return;
    }
    if (stage == 1 && expected) {
        stage = 2;
        waitTicks = 0;
        action->trigger();
        return;
    }
    fprintf(stderr, "SCAN_TEST PASS tracks=%d repeat=%d\n", expected, stage == 2);
    stage = 99;
    qApp->quit();
}
void start() {
    qunsetenv("LD_PRELOAD");
    QTimer::singleShot(8000, qApp, [] {auto*t=new QTimer(qApp);QObject::connect(t,&QTimer::timeout,qApp,&step);t->start(100); });
}
Q_COREAPP_STARTUP_FUNCTION(start)
} // namespace
