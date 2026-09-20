#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>
#include <QScreen>
#include <QFile>
#include <QStandardPaths>
#include <QtTest/QTest>
#include <algorithm>
#include <cstdio>
namespace {
QMainWindow* mainWindow;
QWidget* systemPanel;
QList<QWidget*> buttons(){
 QList<QWidget*> result;
 for(auto* w:systemPanel->findChildren<QWidget*>()) if(w->isVisible()&&(w->objectName()=="SettingsActionButton"||w->objectName()=="SettingsDangerButton")) result.append(w);
 std::sort(result.begin(),result.end(),[](QWidget*a,QWidget*b){auto x=a->mapTo(mainWindow,QPoint()),y=b->mapTo(mainWindow,QPoint());return x.y()==y.y()?x.x()<y.x():x.y()<y.y();});return result;
}
void tap(int i){auto b=buttons();if(i<0)i=b.size()+i;if(i<0||i>=b.size()){qApp->exit(90);return;}QTest::mouseClick(b[i],Qt::LeftButton);}
void snap(const char* name){mainWindow->screen()->grabWindow(mainWindow->winId()).save(qEnvironmentVariable("POWER_TEST_DIR")+"/"+name+".png");}
void run(){
 for(auto*w:QApplication::topLevelWidgets())if(auto*m=qobject_cast<QMainWindow*>(w)) mainWindow=m;
 if(!mainWindow){qApp->exit(91);return;}
 auto* stack=mainWindow->findChild<QStackedWidget*>("SettingsStack");if(!stack){qApp->exit(92);return;}
 systemPanel=stack->widget(3);stack->setCurrentIndex(3);
 for(QWidget*w=stack;w->parentWidget();w=w->parentWidget())if(auto*s=qobject_cast<QStackedWidget*>(w->parentWidget()))s->setCurrentWidget(w);
 mainWindow->resize(1280,800);mainWindow->show();
 QTimer::singleShot(1000,[]{snap("system");tap(-1);
 QTimer::singleShot(400,[]{snap("power-menu");tap(-2);
 QTimer::singleShot(400,[]{snap("reboot-confirm");tap(-1);
 QTimer::singleShot(400,[]{ // Cancellation returned to normal. Reopen and confirm restart.
 tap(-1);tap(-2);tap(-2);
 QTimer::singleShot(1000,[]{snap("reboot-refused");tap(-1);tap(-3);tap(-2);
 QTimer::singleShot(1000,[]{snap("shutdown-refused");QFile f(qEnvironmentVariable("POWER_TEST_LOG"));f.open(QIODevice::ReadOnly);auto data=f.readAll();bool ok=data=="--no-ask-password reboot\n--no-ask-password poweroff\n";fprintf(stderr,"POWER REQUEST TEST %s: %s\n",ok?"PASS":"FAIL",data.constData());qApp->exit(ok?0:93);});
 });});});});});
}
void start(){qunsetenv("LD_PRELOAD");if(QStandardPaths::findExecutable("systemctl")!=qEnvironmentVariable("POWER_TEST_DIR")+"/bin/systemctl"){qFatal("Unsafe test PATH");}QTimer::singleShot(8000,qApp,run);}
Q_COREAPP_STARTUP_FUNCTION(start)
}
