#include "Editor.h"
#include "Viewport.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <stdexcept>

static int checks = 0;
static void check(bool b, const char *what) {
    ++checks;
    if (!b)
        throw std::runtime_error(what);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Worldbench integration tests");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle("Basic");
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    qmlRegisterType<Viewport>("Worldbench", 1, 0, "VoxelViewport");
    qmlRegisterUncreatableType<Editor>("Worldbench", 1, 0, "WorldEditor", "Application instance only");
    Editor editor;
    QQmlApplicationEngine engine;
    KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.load(QUrl("qrc:/main.qml"));
    try {
        check(!engine.rootObjects().isEmpty(), "QML window loads");
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        check(win, "native Qt window");
        win->requestActivate();
        check(QTest::qWaitForWindowExposed(win), "window exposed");
        auto *v = win->findChild<Viewport *>("voxelViewport");
        check(v && v->width() > 100 && v->height() > 100, "viewport layout");
        check(QTest::qWaitFor([&] { return !editor.meshBusy() && v->frameCounter()->load() > 0; }, 15000),
              "initial GPU render");
        const auto baseline = editor.world().heights;
        QPoint point = v->mapToScene(QPointF(v->width() * .58, v->height() * .61)).toPoint();
        QTest::mouseMove(win, point);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(20);
        check(editor.canUndo() && editor.modified() && editor.world().heights != baseline,
              "viewport click edits authoritative terrain");
        const auto edited = editor.world().heights;
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QTest::qWait(20);
        check(editor.world().heights == baseline && !editor.modified() && editor.canRedo(),
              "Ctrl Z routes to stroke undo");
        QTest::keyClick(win, Qt::Key_Y, Qt::ControlModifier);
        QTest::qWait(20);
        check(editor.world().heights == edited, "Ctrl Y redo");
        QTemporaryDir tmp;
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("ui.wbench"))) && !editor.modified(),
              "save state");
        editor.newWorld();
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("ui.wbench"))) && editor.world().heights == edited,
              "reopen edited project");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000),
              "world reset discards obsolete mesh jobs");
        point = v->mapToScene(QPointF(v->width() * .5, v->height() * .6)).toPoint();
        v->forceActiveFocus();
        const auto beforeCamera = v->camera();
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        QTest::keyPress(win, Qt::Key_W);
        QTest::qWait(150);
        QTest::keyRelease(win, Qt::Key_W);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        check((v->camera() - beforeCamera).length() > 1, "RMB W fly camera");
        check(editor.world().heights == edited, "camera flight does not sculpt");
        auto viewBefore = v->viewProjection();
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(20, 0), 20);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        check(v->viewProjection() != viewBefore, "RMB mouse look");
        auto orbitBefore = v->camera();
        QTest::mousePress(win, Qt::LeftButton, Qt::AltModifier, point);
        QTest::mouseMove(win, point + QPoint(20, 0), 20);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::AltModifier, point);
        check((v->camera() - orbitBefore).length() > .1 && editor.world().heights == edited,
              "Alt orbit without sculpting");
        auto panBefore = v->camera();
        QTest::mousePress(win, Qt::MiddleButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(15, 10), 20);
        QTest::mouseRelease(win, Qt::MiddleButton, Qt::NoModifier, point);
        check((v->camera() - panBefore).length() > .1, "MMB pan");
        auto stopped = v->camera();
        QTest::qWait(100);
        check((v->camera() - stopped).length() < .001, "flight stops after release");
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        QTest::keyPress(win, Qt::Key_E);
        QTest::qWait(80);
        QTest::keyRelease(win, Qt::Key_E);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        check(v->camera().y() > stopped.y(), "RMB E ascends");
        QTest::keyClick(win, Qt::Key_BracketRight);
        check(editor.radius() == 5, "bracket changes brush radius");
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        QTest::keyPress(win, Qt::Key_W);
        QTest::qWait(50);
        QFocusEvent focusLost(QEvent::FocusOut);
        QCoreApplication::sendEvent(v, &focusLost);
        stopped = v->camera();
        QTest::qWait(100);
        check((v->camera() - stopped).length() < .001, "focus loss stops stuck movement");
        QTest::keyRelease(win, Qt::Key_W);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        v->resetCamera();
        editor.setTool(2);
        point = v->mapToScene(QPointF(v->width() * .58, v->height() * .61)).toPoint();
        const auto previous = editor.world().overrides;
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, point);
        check(editor.world().overrides != previous, "volume carve through viewport");
        editor.undo();
        check(editor.world().overrides == previous, "volume undo");
        QElapsedTimer elapsed;
        elapsed.start();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "dirty mesh drains");
        check(!win->grabWindow().isNull(), "render after editing");
        std::cout << "PASS " << checks << " UI integration checks; mesh settle " << elapsed.elapsed()
                  << " ms\n";
        win->hide();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
