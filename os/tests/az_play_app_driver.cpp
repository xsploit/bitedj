// Test-only Qt driver for the actual BiteDJ AZ play layout. No engine code is replaced.
#include <QApplication>
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QtTest/QTest>
#include <cstdio>
#include <stdexcept>
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
QWidget* visible(QMainWindow* main, const QString& name) {
    for (auto* w : main->findChildren<QWidget*>(name))
        if (w->isVisible())
            return w;
    return nullptr;
}
void run() {
    try {
        QMainWindow* main = nullptr;
        for (auto* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMainWindow*>(w))
                main = m;
        require(main, "main window");
        main->resize(1280, 800);
        main->show();
        auto click = [&](const QString& name) {auto* w=visible(main,name);require(w,"visible touch target");QTest::mouseClick(w,Qt::LeftButton);QTest::qWait(180); };
        click("AzTabButtonPlay");
        auto capture = [&](const QString& suffix) {QTest::qWait(150);require(main->screen()->grabWindow(main->winId()).save(qEnvironmentVariable("AZ_CAPTURE")+suffix+".png"),"screenshot"); };
        auto* left = visible(main, "PlayerToggleLeft");
        auto* right = visible(main, "PlayerToggleRight");
        require(left && right, "both handles present");
        if (qEnvironmentVariableIntValue("AZ_EXPECT_COLLAPSED")) {
            require(left->property("value").toDouble() == 0 && right->property("value").toDouble() == 0, "both collapsed states restored after restart");
            require(!visible(main, "MainViewDeckInfoStack") && !visible(main, "BeatFX_Container"), "restored layout hides panels");
            capture("-restored");
            std::fprintf(stderr, "AZ_PLAY PERSISTENCE PASS\n");
            qApp->exit(0);
            return;
        }
        if (left->property("value").toDouble() == 0)
            click("PlayerToggleLeft");
        if (right->property("value").toDouble() == 0)
            click("PlayerToggleRight");
        auto* wave = visible(main, "Waveform1");
        require(wave, "waveform exists");
        auto* wave2 = visible(main, "Waveform2");
        require(wave2, "second waveform exists");
        const int fullWidth = wave->width();
        const auto* originalWave = wave;
        capture("-expanded");
        click("PlayerToggleLeft");
        require(!visible(main, "MainViewDeckInfoStack"), "left rails hidden");
        require(visible(main, "BeatFX_Container"), "right remains visible");
        const int leftHiddenWidth = wave->width();
        require(leftHiddenWidth > fullWidth, "left space becomes waveform space");
        capture("-left-hidden");
        click("PlayerToggleRight");
        require(!visible(main, "BeatFX_Container"), "right tools hidden");
        const int bothHiddenWidth = wave->width();
        require(bothHiddenWidth > leftHiddenWidth, "right space becomes waveform space");
        require(visible(main, "Waveform1") == originalWave, "renderer widget reused");
        require(visible(main, "PlayerToggleLeft") && visible(main, "PlayerToggleRight"), "reopen handles remain visible");
        capture("-both-hidden");
        click("PlayerToggleLeft");
        require(visible(main, "MainViewDeckInfoStack"), "left restored independently");
        require(!visible(main, "BeatFX_Container"), "right stays hidden");
        click("PlayerToggleRight");
        require(wave->width() == fullWidth, "original waveform width restored");
        // Folding must preserve the selected layout, not just the rail width.
        auto requireAzRails = [&] {
            const auto rails = main->findChildren<QStackedWidget*>("MainViewDeckInfoStack");
            require(rails.size() == 2, "two deck rail stacks");
            for (auto* rail : rails) {
                require(rail->isVisible() && rail->currentIndex() == 3,
                        "reopened rail retains AZ layout");
            }
            require(visible(main, "AzPlay1") && visible(main, "AzPlay2"),
                    "both AZ transport rows remain visible");
            require(!visible(main, "WaveformInfo"), "default rail remains hidden");
        };
        requireAzRails();
        for (int repeat = 0; repeat < 5; ++repeat) {
            click("PlayerToggleLeft");
            click("PlayerToggleLeft");
            requireAzRails();
        }
        capture("-az-reopened");
        // A loaded, analyzed fixture is required: these exercise real control bindings.
        for (const QString channel : {"1", "2"}) {
            auto* time = qobject_cast<QLabel*>(visible(main, "AzTime" + channel));
            require(time, "loaded deck time");
            const auto before = time->text();
            click("AzTime" + channel);
            require(time->text() != before, "elapsed/remaining toggles");
            click("AzTime" + channel);
            for (const QString prefix : {"AzQuantize", "AzMasterTempo"}) {
                auto* button = visible(main, prefix + channel);
                require(button, "toggle control");
                const auto before = button->property("value").toDouble();
                click(prefix + channel);
                require(button->property("value").toDouble() != before, "toggle changed actual bound control");
                click(prefix + channel);
            }
            click("AzLoopToggle" + channel);
            require(visible(main, "AzLoopExit" + channel), "loop entered");
            click("AzLoopExit" + channel);
            require(visible(main, "AzLoopToggle" + channel), "loop exited");
            const auto beforeJump = time->text();
            click("AzJumpForward" + channel);
            require(time->text() != beforeJump, "beat jump moved position");
            click("AzJumpBack" + channel);
            click("AzHotcue" + channel + "_1");
            require(visible(main, "AzHotcue" + channel + "_1")->property("value").toDouble() > 0, "hot cue stored");
        }
        const int waveHeight = wave->height();
        click("AzCueDrawer1");
        require(visible(main, "CuePanel_Close"), "cue drawer opens");
        require(wave->height() == waveHeight, "drawer preserves waveform height");
        click("CuePanel_Close");
        click("FxPanel_TabWave");
        require(visible(main, "WavePanel_ZoomIn"), "zoom tools open");
        click("WavePanel_ZoomIn");
        click("WavePanel_ZoomOut");
        click("FxPanel_TabGrid");
        require(visible(main, "GridPanel_Title"), "grid tools open");
        click("FxPanel_TabFx");
        capture("-checked");
        auto valueText = [&](const QString& name) {
            auto* label = qobject_cast<QLabel*>(visible(main, name));
            require(label, "live rail value present");
            return label->text();
        };
        click("FxPanel_TabLoop");
        click("RailLoopDeck1");
        const auto loopBefore = valueText("RailLoopSize1");
        click("RailLoopDouble1");
        require(valueText("RailLoopSize1") != loopBefore, "loop size changes");
        click("RailLoopHalf1");
        require(valueText("RailLoopSize1") == loopBefore, "loop size restored");
        click("RailLoopToggle1");
        require(visible(main, "RailLoopToggle1")->property("highlight").toDouble() == 1, "rail starts loop");
        click("RailLoopToggle1");
        require(visible(main, "RailLoopToggle1")->property("highlight").toDouble() == 0, "rail exits loop");
        const auto jumpBefore = valueText("RailJumpSize1");
        click("RailJumpDouble1");
        require(valueText("RailJumpSize1") != jumpBefore, "jump size changes");
        click("RailJumpHalf1");
        const auto positionBefore = valueText("AzTime1");
        click("RailJumpForward1");
        require(valueText("AzTime1") != positionBefore, "rail jump moves deck");
        click("RailJumpBack1");
        capture("-rail-loop");
        click("RailLoopDeck2");
        require(!visible(main, "RailLoopSize1") && visible(main, "RailLoopSize2"), "loop deck selection isolates controls");
        click("FxPanel_TabKey");
        const auto keyBefore = valueText("RailPitch1");
        const auto otherKey = valueText("RailPitch2");
        click("RailKeyUp1");
        require(valueText("RailPitch1") != keyBefore && valueText("RailPitch2") == otherKey, "key shift affects selected deck only");
        capture("-rail-key");
        click("RailKeyReset1");
        require(valueText("RailPitch1") == keyBefore, "key reset restored");
        click("FxPanel_TabWave");
        const auto zoomBefore = valueText("RailZoom");
        click("WavePanel_ZoomIn");
        require(valueText("RailZoom") != zoomBefore, "zoom value follows control");
        click("WavePanel_ZoomOut");
        click("RailGainBold");
        require(visible(main,"RailGainBold")->property("displayValue").toDouble() == 1, "waveform height preset selected");
        require(wave->width() == fullWidth, "wave tools preserve waveform width");
        capture("-rail-wave");
        click("FxPanel_TabGrid");
        const auto gridBefore = valueText("RailGridBpm1");
        click("RailGridDouble1");
        require(valueText("RailGridBpm1") != gridBefore, "grid tempo doubled");
        click("RailGridHalf1");
        require(valueText("RailGridBpm1") == gridBefore, "grid tempo restored");
        capture("-rail-grid");
        click("FxPanel_TabFx");
        auto* selector = qobject_cast<QComboBox*>(visible(main,"EffectSelector"));
        require(selector, "native effect selector");
        const int echo = selector->findText("Echo", Qt::MatchContains);
        require(echo >= 0, "Echo available");
        selector->setCurrentIndex(echo);
        QMetaObject::invokeMethod(selector, "activated", Q_ARG(int,echo));
        QTest::qWait(250);
        auto dragKnob = [&](const QString& knobName, const QString& numberName) {
            const auto before = valueText(numberName);
            auto* k = visible(main,knobName); require(k,"effect knob present");
            QTest::mousePress(k,Qt::LeftButton,Qt::NoModifier,QPoint(26,26));
            QTest::mouseMove(k,QPoint(26,48),50);
            QTest::mouseRelease(k,Qt::LeftButton,Qt::NoModifier,QPoint(26,48));
            QTest::qWait(150);
            require(valueText(numberName) != before, "effect knob changes actual value");
        };
        dragKnob("RailMixKnob","RailMixValue");
        dragKnob("RailParamKnob1","RailParamValue1");
        capture("-rail-fx");
        auto* scroll = main->findChild<QScrollArea*>("RailScroll0");
        require(scroll && scroll->verticalScrollBar()->maximum() > 0, "long FX page scrolls");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QTest::qWait(200);
        auto* effectSwitch = visible(main,"RailEffectSwitch1");
        require(effectSwitch,"loaded FX switch visible");
        const auto switchBefore = effectSwitch->property("value").toDouble();
        click("RailEffectSwitch1");
        require(effectSwitch->property("value").toDouble() != switchBefore, "effect switch controls native parameter");
        capture("-rail-fx-bottom");
        scroll->verticalScrollBar()->setValue(0);
        require(wave->height() == waveHeight, "all rail pages preserve waveform height");
        // Exercise the actual settings path and return to the new layout.
        click("AzTabButtonSettings");
        auto subtabs = main->findChildren<QWidget*>("SettingsSubtabButton");
        require(subtabs.size() > 1, "settings subtabs");
        QTest::mouseClick(subtabs[1], Qt::LeftButton);
        QTest::qWait(180);
        auto* azChoice = visible(main, "AzLayoutChoice");
        require(azChoice, "AZ layout choice");
        QWidget* nativeChoice = nullptr;
        for (auto* w : azChoice->parentWidget()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            if (QString(w->metaObject()->className()) == "WPushButton") {
                nativeChoice = w;
                break;
            }
        require(nativeChoice, "native layout choice");
        QTest::mouseClick(nativeChoice, Qt::LeftButton);
        QTest::qWait(180);
        click("TabButtonPlay");
        require(!visible(main, "AzDecks"), "legacy deck layout restored");
        require(visible(main, "Decks"), "legacy deck cards present");
        require(visible(main, "CuePanel_Stack")->height() == 180, "legacy drawer height restored");
        capture("-legacy");
        click("TabButtonSettings");
        click("AzLayoutChoice");
        click("AzTabButtonPlay");
        require(visible(main, "Waveform1") == originalWave, "changing layout reuses waveform");
        require(visible(main, "AzDecks"), "AZ cards restored");
        click("PlayerToggleLeft");
        click("PlayerToggleRight");
        QFile result(qEnvironmentVariable("AZ_CAPTURE") + ".json");
        require(result.open(QIODevice::WriteOnly), "result open");
        result.write(QJsonDocument(QJsonObject{{"passed", true}, {"fullWidth", fullWidth}, {"leftHiddenWidth", leftHiddenWidth}, {"bothHiddenWidth", bothHiddenWidth}, {"rendererReused", true}, {"physicalTouchTested", false}}).toJson());
        result.close();
        std::fprintf(stderr, "AZ_PLAY PASS widths=%d/%d/%d\n", fullWidth, leftHiddenWidth, bothHiddenWidth);
        qApp->exit(0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "AZ_PLAY FAIL %s\n", e.what());
        qApp->exit(91);
    }
}
void start() {
    qunsetenv("LD_PRELOAD");
    QTimer::singleShot(10000, qApp, run);
}
Q_COREAPP_STARTUP_FUNCTION(start)
} // namespace
