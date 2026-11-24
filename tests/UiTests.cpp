#include "Editor.h"
#include "Viewport.h"
#include "core/Project.h"
#include <QBuffer>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <private/qzipwriter_p.h>
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
    QTemporaryDir settingsRoot;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());
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
        QJSEngine expressionEngine;
        QFile expressionSource(":/Arithmetic.js");
        check(expressionSource.open(QIODevice::ReadOnly), "arithmetic module loads");
        auto script = QString::fromUtf8(expressionSource.readAll()).replace(".pragma library", "");
        check(!expressionEngine.evaluate(script).isError(), "arithmetic module parses");
        auto calculate = expressionEngine.globalObject().property("calculate");
        check(calculate.call({QString("(256+128)/2")}).toNumber() == 192 &&
                  calculate.call({QString("-2*(3+4)")}).toNumber() == -14,
              "arithmetic precedence and signed parentheses");
        for (const auto &bad :
             {QString("1/0"), QString("foo()"), QString("1+"), QString("2**3"), QString(129, '1')})
            check(std::isnan(calculate.call({bad}).toNumber()), "invalid or oversized expression rejected");
        check(!engine.rootObjects().isEmpty(), "QML window loads");
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        check(win, "native Qt window");
        win->requestActivate();
        check(QTest::qWaitForWindowExposed(win), "window exposed");
        auto *chrome = win->findChild<QQuickItem *>("windowChrome");
        auto *menus = win->findChild<QQuickItem *>("mainMenuBar");
        check(chrome && menus && win->flags().testFlag(Qt::FramelessWindowHint) &&
                  chrome->mapToScene({0, 0}).y() == 0 && menus->mapToScene({0, 0}).y() >= chrome->height(),
              "custom dark title bar precedes menu and removes native frame");
        auto *minimize = win->findChild<QQuickItem *>("windowMinimize");
        auto *maximize = win->findChild<QQuickItem *>("windowMaximize");
        auto *closeButton = win->findChild<QQuickItem *>("windowClose");
        check(minimize && maximize && closeButton && minimize->width() == maximize->width() &&
                  maximize->width() == closeButton->width(),
              "window controls have equal widths");
        std::function<QQuickItem *(QQuickItem *, const QString &)> visualItem =
            [&](QQuickItem *root, const QString &name) -> QQuickItem * {
            if (root->objectName() == name)
                return root;
            for (auto *child : root->childItems())
                if (auto *found = visualItem(child, name))
                    return found;
            return nullptr;
        };
        auto *v = win->findChild<Viewport *>("voxelViewport");
        check(v && v->width() > 100 && v->height() > 100, "viewport layout");
        // QTest mouse events are synthetic Qt events, not WM_INPUT hardware packets.
        v->setRawInput(false);
        check(QTest::qWaitFor([&] { return !editor.meshBusy() && v->frameCounter()->load() > 0; }, 15000),
              "initial GPU render");
        check(win->findChild<QQuickItem *>("sceneOutliner"), "actual outliner QML loaded");
        auto *brushPanel = win->findChild<QQuickItem *>("leftBrushSettings");
        check(brushPanel && brushPanel->isVisible() &&
                  brushPanel->mapToScene({0, 0}).x() < v->mapToScene({0, 0}).x(),
              "brush settings expand to left of viewport");
        check(editor.sceneNodes().size() == 6 && editor.sceneNodes()[0].toMap()["name"] == "基础地形层1",
              "outliner contains terrain and environment scene components");
        editor.setTool(-1);
        QTest::qWait(40);
        check(!brushPanel->isVisible(), "navigation collapses brush settings");
        // Tab changes must reuse the scene viewport and its camera. A modal
        // creation dialog or a Loader here used to obscure/reset the workspace.
        const auto tabCamera = v->camera();
        auto *diagnostics = win->findChild<QQuickItem *>("viewportDiagnostics");
        check(diagnostics && !diagnostics->isVisible(), "technical viewport statistics start collapsed");
        check(win->property("openAssetCount").toInt() == 0, "scene is the only initial tab");
        QMetaObject::invokeMethod(win, "openAsset", Q_ARG(QVariant, QString("@road")));
        QTest::qWait(80);
        check(!v->isVisible() && win->property("activeId") == "@road", "road editor opens on demand");
        auto *closeRoad = visualItem(win->contentItem(), "closeTab-@road");
        check(closeRoad, "road editor tab has a close button");
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, closeRoad->mapToScene({10, 12}).toPoint());
        QTest::qWait(80);
        check(win->property("openAssetCount").toInt() == 0 && v->isVisible() &&
                  win->findChild<Viewport *>("voxelViewport") == v && v->camera() == tabCamera,
              "closing the editor returns to the original scene camera");
        check(win->findChild<QQuickItem *>("sceneOutliner")->isVisible(),
              "returning to scene restores the docked outliner");
        auto *library = win->findChild<QQuickItem *>("assetShelf");
        check(library && !library->property("expanded").toBool(), "asset library starts collapsed");
        const auto fullHeight = v->height();
        library->setProperty("expanded", true);
        QTest::qWait(80);
        check(v->height() < fullHeight && v->height() > 250 && v->camera() == tabCamera,
              "opening asset library reallocates viewport without changing camera");
        library->setProperty("expanded", false);
        QTest::qWait(80);
        v->forceActiveFocus();
        QTest::keyClick(win, Qt::Key_1);
        check(editor.tool() == 0, "first 1 selects raise");
        for (int expected : {1, 2, 3, 4, 5, 6, -1, 0}) {
            QTest::keyClick(win, Qt::Key_1);
            check(editor.tool() == expected, "repeated 1 cycles every tool including navigation");
        }
        QTest::keyClick(win, Qt::Key_5);
        check(editor.tool() == 4, "5 selects material painting");
        QTest::keyClick(win, Qt::Key_1);
        check(editor.tool() == 0, "1 after another shortcut starts with raise");
        QKeyEvent repeated(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QString(), true);
        QCoreApplication::sendEvent(v, &repeated);
        check(editor.tool() == 0, "held key repeat is throttled");
        QTest::qWait(240);
        QCoreApplication::sendEvent(v, &repeated);
        check(editor.tool() == 1, "held key cycles after repeat interval");
        editor.setShortcut("toolPaint", "9");
        QTest::keyClick(win, Qt::Key_9);
        check(editor.tool() == 4, "tool shortcuts use saved preferences");
        editor.resetShortcuts();
        QTest::keyClick(win, Qt::Key_0);
        check(editor.tool() == -1, "0 returns to navigation");
        editor.setTool(0);
        QTest::qWait(40);
        check(editor.selectedNode() == "terrain-base-1", "tool selection preserves scene selection");
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
        v->setFlightInertia(true);
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        const auto accelerationStart = v->camera();
        QTest::keyPress(win, Qt::Key_W);
        QTest::qWait(80);
        const auto accelerationMiddle = v->camera();
        QTest::qWait(80);
        const auto accelerationEnd = v->camera();
        check((accelerationEnd - accelerationMiddle).length() >
                  (accelerationMiddle - accelerationStart).length(),
              "fly velocity accelerates gradually");
        QTest::keyRelease(win, Qt::Key_W);
        const auto brakingStart = v->camera();
        QTest::qWait(80);
        check((v->camera() - brakingStart).length() > .1,
              "direction release decelerates instead of stopping abruptly");
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        const auto inertiaStopped = v->camera();
        QTest::qWait(100);
        check(v->camera() == inertiaStopped, "RMB release immediately cancels flight inertia");
        auto viewBefore = v->viewProjection();
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(20, 0), 20);
        QTest::qWait(40);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point + QPoint(20, 0));
        check(v->viewProjection() != viewBefore, "RMB mouse look");
        auto orbitBefore = v->camera();
        QTest::mousePress(win, Qt::LeftButton, Qt::AltModifier, point);
        QTest::mouseMove(win, point + QPoint(20, 0), 20);
        QTest::qWait(40);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::AltModifier, point + QPoint(20, 0));
        check((v->camera() - orbitBefore).length() > .1 && editor.world().heights == edited,
              "Alt orbit without sculpting");
        // Identical orbit deltas must be independent of the cursor's initial location.
        auto orbitAt = [&](QPointF local) {
            v->resetCamera();
            const auto start = v->mapToScene(local).toPoint();
            const auto initial = v->camera();
            const auto matrix = v->viewProjection();
            QTest::mousePress(win, Qt::LeftButton, Qt::AltModifier, start);
            check(v->camera() == initial && v->viewProjection() == matrix, "orbit press has zero snap");
            const auto pivot = v->orbitPivot();
            const auto centered = (matrix * QVector4D(pivot, 1)).toVector3DAffine();
            check(std::abs(centered.x()) < .001 && std::abs(centered.y()) < .001, "orbit pivot on view axis");
            QTest::mouseMove(win, start + QPoint(1, 0), 10);
            check((v->camera() - initial).length() < 1, "first orbit pixel stays continuous");
            QTest::mouseMove(win, start + QPoint(20, 10), 20);
            QTest::mouseRelease(win, Qt::LeftButton, Qt::AltModifier, start + QPoint(20, 10));
            check(std::abs((v->camera() - pivot).length() - (initial - pivot).length()) < .001,
                  "orbit radius preserved");
            const auto after = (v->viewProjection() * QVector4D(pivot, 1)).toVector3DAffine();
            check(std::abs(after.x()) < .001 && std::abs(after.y()) < .001, "pivot remains at view center");
            return v->camera();
        };
        auto orbitLeft = orbitAt({v->width() * .2, v->height() * .3});
        auto orbitRight = orbitAt({v->width() * .75, v->height() * .75});
        check((orbitLeft - orbitRight).length() < .001, "cursor position cannot change orbit");
        v->applyCamera({{"position", QJsonArray{10, 100, 10}}, {"yaw", 0}, {"pitch", 45}});
        auto skyStart = v->camera();
        QTest::mousePress(win, Qt::LeftButton, Qt::AltModifier, point);
        QTest::mouseMove(win, point + QPoint(1, 0), 10);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::AltModifier, point + QPoint(1, 0));
        check((v->camera() - skyStart).length() < 1, "sky orbit uses continuous view-axis fallback");
        // A surface closer than five blocks must not be pushed away on first movement.
        int surface = editor.world().height(0, 0);
        v->applyCamera({{"position", QJsonArray{.5, surface + 2., .5}}, {"yaw", 0}, {"pitch", -89}});
        auto nearStart = v->camera();
        QTest::mousePress(win, Qt::LeftButton, Qt::AltModifier, point);
        QTest::mouseMove(win, point + QPoint(1, 0), 10);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::AltModifier, point + QPoint(1, 0));
        check((v->camera() - nearStart).length() < .1, "near-surface orbit has no minimum-radius snap");
        v->resetCamera();
        auto panBefore = v->camera();
        QTest::mousePress(win, Qt::MiddleButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(15, 10), 20);
        QTest::qWait(40);
        QTest::mouseRelease(win, Qt::MiddleButton, Qt::NoModifier, point + QPoint(15, 10));
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
        v->setRawInput(true);
        v->forceActiveFocus();
        win->requestActivate();
        QTest::qWait(80);
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, point);
        check(v->rawActive(), "Windows raw input registration and capture");
        QFocusEvent rawFocusLost(QEvent::FocusOut);
        QCoreApplication::sendEvent(v, &rawFocusLost);
        check(!v->rawActive(), "raw input released on focus loss");
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, point);
        v->setRawInput(false);

        // Exercise the real stdio process, local transport, protocol handshake and shared history.
        editor.setMcpEnabled(true);
        const auto config = QJsonDocument::fromJson(editor.mcpConfiguration().toUtf8())
                                .object()["mcpServers"]
                                .toObject()["worldbench"]
                                .toObject();
        QProcess bridge;
        QStringList bridgeArgs;
        for (const auto &a : config["args"].toArray())
            bridgeArgs << a.toString();
        bridge.start(config["command"].toString(), bridgeArgs);
        check(bridge.waitForStarted(5000), "MCP stdio bridge starts");
        int requestId = 0;
        QByteArray incoming;
        auto request = [&](QString method, QJsonObject params) -> QJsonObject {
            auto id = ++requestId;
            QJsonObject request{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
            bridge.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
            check(QTest::qWaitFor(
                      [&] {
                          incoming += bridge.readAllStandardOutput();
                          return incoming.contains('\n') || bridge.state() == QProcess::NotRunning;
                      },
                      10000),
                  "MCP response arrives");
            int end = incoming.indexOf('\n');
            check(end >= 0, "MCP emits JSON line");
            auto data = incoming.left(end);
            incoming.remove(0, end + 1);
            auto response = QJsonDocument::fromJson(data).object();
            if (response["id"].toInt() != id)
                std::cerr << "Expected " << id << ", received " << data.left(400).constData() << "\n";
            check(response["id"].toInt() == id, "MCP response ID");
            return response;
        };
        auto init = request("initialize",
                            {{"protocolVersion", "2025-06-18"},
                             {"capabilities", QJsonObject{}},
                             {"clientInfo", QJsonObject{{"name", "Worldbench tests"}, {"version", "1"}}}});
        check(init["result"].toObject()["protocolVersion"] == "2025-06-18", "MCP negotiation");
        bridge.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        auto tools = request("tools/list", {});
        check(tools["result"].toObject()["tools"].toArray().size() == 16, "MCP discoverable tools");
        auto call = [&](QString name, QJsonObject args) {
            return request("tools/call", {{"name", name}, {"arguments", args}})["result"].toObject();
        };
        auto sceneResult = call("world_info", {});
        auto sceneInfo = QJsonDocument::fromJson(
                             sceneResult["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                             .object();
        check(sceneInfo["scene_nodes"].toArray().size() == 6 &&
                  sceneInfo["scene_nodes"].toArray()[0].toObject()["id"] == "terrain-base-1",
              "MCP uses same actual scene nodes as outliner");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "environment test mesh ready");
        const auto envScene = editor.scene();
        const auto envBlocks = editor.world().overrides;
        const auto envHeights = editor.world().heights;
        const auto envBefore = editor.world().environment;
        auto envResult = call("environment", {{"changes", QJsonObject{{"enabled", true},
                                                                      {"azimuth", 75},
                                                                      {"elevation", 20},
                                                                      {"haze", 40},
                                                                      {"clouds", true},
                                                                      {"cloudDensity", 70},
                                                                      {"cloudSpeed", 35},
                                                                      {"waterWaves", false},
                                                                      {"waterReflections", false},
                                                                      {"shadows", false},
                                                                      {"waveStrength", 62},
                                                                      {"waveDensity", 73},
                                                                      {"waveSpeed", 22},
                                                                      {"waterDepthFade", 80},
                                                                      {"waterFoamStrength", 75},
                                                                      {"waterFoamWidth", 8}}},
                                              {"expected_revision", double(editor.revision())}});
        check(!envResult["isError"].toBool() && editor.world().environment.enabled &&
                  editor.world().environment.azimuth == 75,
              "stdio MCP environment change");
        check(!editor.world().environment.waterWaves && !editor.world().environment.waterReflections &&
                  !editor.world().environment.shadows && editor.world().environment.waveStrength == 62 &&
                  editor.world().environment.waveDensity == 73 &&
                  editor.world().environment.waveSpeed == 22 &&
                  editor.world().environment.cloudDensity == 70 &&
                  editor.world().environment.waterFoamWidth == 8,
              "real stdio MCP accepts water and shadow controls");
        check(editor.scene() == envScene && !editor.meshBusy() && editor.world().overrides == envBlocks &&
                  editor.world().heights == envHeights,
              "lighting does not rebuild or mutate world");
        auto envRevision = editor.revision();
        envResult = call("environment", {{"changes", QJsonObject{{"elevation", 91}, {"haze", 0}}},
                                         {"expected_revision", double(envRevision)}});
        check(envResult["isError"].toBool() && editor.revision() == envRevision &&
                  editor.world().environment.haze == 40,
              "invalid lighting batch rejected atomically");
        envResult = call("environment", {{"changes", QJsonObject{{"sun", false}}},
                                         {"expected_revision", double(envRevision - 1)}});
        check(envResult["isError"].toBool() && editor.world().environment.sun,
              "stale environment revision rejected");
        editor.setSelectedNode("environment-sun");
        QTest::qWait(100);
        auto *envPanel = visualItem(win->contentItem(), "environmentControls");
        auto *envAngle = visualItem(win->contentItem(), "environment_azimuth");
        check(envPanel && envPanel->isVisible() && envAngle && envAngle->isVisible(),
              "selected sun shows contextual controls");
        check(win->grabWindow().save("test-output/environment-0.13.0.png"),
              "capture lighting controls and actual shader");
        auto *envInput = qobject_cast<QQuickItem *>(envAngle->property("contentItem").value<QObject *>());
        check(envInput, "environment numeric input exists");
        const auto envDragPoint =
            envInput->mapToScene({envInput->width() / 2, envInput->height() / 2}).toPoint();
        QTest::mousePress(envInput->window(), Qt::LeftButton, Qt::NoModifier, envDragPoint);
        QTest::mouseMove(envInput->window(), envDragPoint + QPoint(30, 0), 30);
        QTest::mouseRelease(envInput->window(), Qt::LeftButton, Qt::NoModifier, envDragPoint + QPoint(30, 0));
        check(editor.world().environment.azimuth != 75 && v->hasActiveFocus(),
              "environment scrub commits and returns focus");
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        check(editor.world().environment.azimuth == 75, "immediate keyboard undo after environment scrub");
        editor.undo();
        check(editor.world().environment == envBefore && editor.scene() == envScene,
              "environment undo uses no mesh rebuild");
        editor.redo();
        check(editor.world().environment.enabled, "environment redo");
        editor.undo();
        editor.beginEnvironmentEdit();
        editor.setEnvironment({{"azimuth", 15}});
        editor.setEnvironment({{"azimuth", 20}});
        check(editor.commandBusy(), "lighting scrub protects MCP transaction");
        editor.endEnvironmentEdit();
        editor.undo();
        check(editor.world().environment == envBefore, "lighting scrub is one undo command");
        editor.beginEnvironmentEdit();
        editor.setEnvironment({{"exposure", 5}});
        editor.endEnvironmentEdit(true);
        check(editor.world().environment == envBefore, "cancel lighting scrub");
        QSignalSpy environmentSignals(&editor, &Editor::changed);
        editor.beginEnvironmentEdit();
        editor.endEnvironmentEdit();
        check(environmentSignals.size() >= 2 && !editor.commandBusy(),
              "unchanged lighting scrub clears UI busy state");
        editor.setSelectedNode("terrain-base-1");
        auto beforeBlocks = editor.world().overrides;
        auto revision = editor.revision();
        auto result = call("fill_box", {{"min", QJsonArray{0, 90, 0}},
                                        {"max", QJsonArray{2, 92, 2}},
                                        {"block", "minecraft:oak_planks"},
                                        {"expected_revision", double(revision)}});
        check(!result["isError"].toBool() && editor.world().block(1, 91, 1) == wb::Planks,
              "MCP fill edits world");
        check(editor.revision() > revision && editor.canUndo(), "MCP revision and undo");
        result = call("fill_box", {{"min", QJsonArray{0, 90, 0}},
                                   {"max", QJsonArray{2, 92, 2}},
                                   {"block", "minecraft:stone"},
                                   {"expected_revision", double(revision)}});
        check(result["isError"].toBool() && editor.world().block(1, 91, 1) == wb::Planks,
              "stale revision cannot overwrite user work");
        auto unchanged = editor.world().overrides;
        revision = editor.revision();
        result = call(
            "set_blocks",
            {{"blocks",
              QJsonArray{QJsonObject{{"position", QJsonArray{0, 100, 0}}, {"block", "minecraft:stone"}},
                         QJsonObject{{"position", QJsonArray{99999, 100, 0}}, {"block", "minecraft:stone"}}}},
             {"expected_revision", double(revision)}});
        check(result["isError"].toBool() && editor.world().overrides == unchanged &&
                  editor.revision() == revision,
              "invalid batch has zero partial changes");
        result = call("undo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().overrides == beforeBlocks,
              "one MCP fill undoes as one command");
        result = call("read_region", {{"min", QJsonArray{0, 90, 0}}, {"max", QJsonArray{2, 92, 2}}});
        check(!result["isError"].toBool(), "MCP region read");
        QJsonArray house;
        for (int y = 80; y <= 84; ++y)
            for (int z = 10; z <= 16; ++z)
                for (int x = 10; x <= 16; ++x) {
                    if (y != 80 && y != 84 && x != 10 && x != 16 && z != 10 && z != 16)
                        continue;
                    if (z == 16 && x == 13 && (y == 81 || y == 82))
                        continue; // Doorway.
                    house << QJsonObject{{"position", QJsonArray{x, y, z}},
                                         {"block", y == 82 && x == 10 && z == 13 ? "minecraft:glass"
                                                                                 : "minecraft:oak_planks"}};
                }
        result = call("set_blocks", {{"blocks", house}, {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().block(10, 82, 13) == wb::Glass &&
                  editor.world().block(13, 81, 16) == wb::Air && editor.world().block(13, 82, 13) == wb::Air,
              "MCP constructs hollow house with doorway and window");
        result = call("undo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().overrides == beforeBlocks,
              "MCP building undone atomically");
        result = call("redo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().block(10, 82, 13) == wb::Glass,
              "MCP building redo");
        result = call("catalog", {{"query", "stone"}});
        check(!result["isError"].toBool(), "MCP block catalog");
        result = call("camera", {{"position", QJsonArray{24, 92, 32}}, {"yaw", -30}, {"pitch", -24}});
        check(!result["isError"].toBool() && v->camera() == QVector3D(24, 92, 32), "MCP camera control");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 10000), "MCP mesh drains");
        result = call("screenshot", {});
        check(!result["isError"].toBool() && result["content"].toArray()[0].toObject()["type"] == "image",
              "MCP image result");
        const auto screenshot = result["content"].toArray()[0].toObject();
        QImage houseImage;
        check(
            houseImage.loadFromData(QByteArray::fromBase64(screenshot["data"].toString().toLatin1()), "PNG"),
            "decode real MCP screenshot");
        QDir().mkpath("test-output");
        check(houseImage.save("test-output/mcp-house-0.13.0.png"), "capture MCP house");
        const int caveY = editor.world().height(-20, -20);
        result = call("fill_box", {{"min", QJsonArray{-20, caveY - 4, -20}},
                                   {"max", QJsonArray{-20, caveY, -20}},
                                   {"block", "minecraft:air"},
                                   {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool(), "MCP creates flatten cavity fixture");
        QJsonObject flatten{{"position", QJsonArray{-20, caveY, -20}},
                            {"tool", 1},
                            {"radius", 1},
                            {"strength", 4},
                            {"fill_holes", false},
                            {"expected_revision", double(editor.revision())}};
        result = call("sculpt", flatten);
        check(!result["isError"].toBool() && !editor.world().isAir(editor.world().block(-20, caveY, -20)) &&
                  editor.world().isAir(editor.world().block(-20, caveY - 1, -20)),
              "MCP flatten caps surface and preserves cavity");
        flatten["fill_holes"] = true;
        flatten["fill_depth"] = 2;
        flatten["expected_revision"] = double(editor.revision());
        result = call("sculpt", flatten);
        check(!result["isError"].toBool() && editor.world().block(-20, caveY - 2, -20) == wb::Stone &&
                  editor.world().isAir(editor.world().block(-20, caveY - 3, -20)),
              "MCP fill depth preserves deeper cavity");
        check(editor.createWorld(16384, 42, 24, 0, -60, true, 12289), "MCP large rectangular fixture");
        result =
            call("set_blocks", {{"blocks", QJsonArray{QJsonObject{{"position", QJsonArray{8000, 200, 6000}},
                                                                  {"block", "minecraft:glass"}}}},
                                {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().block(8000, 200, 6000) == wb::Glass,
              "real MCP bridge edits distant 64-bit voxel coordinates");
        result = call("undo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().block(8000, 200, 6000) == wb::Air,
              "MCP distant edit undo uses full key");
        check(editor.createWorld(128, 42, 24, 0, -60, true), "terrain UI fixture");
        QImage heightImage(64, 64, QImage::Format_Grayscale16);
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) {
                double dx = (x - 31.5) / 20, dz = (z - 31.5) / 20;
                reinterpret_cast<quint16 *>(heightImage.scanLine(z))[x] =
                    quint16(65535 * std::exp(-(dx * dx + dz * dz)));
            }
        check(heightImage.save(tmp.filePath("hill.png")), "heightmap UI source");
        const auto beforeImport = editor.world().height(0, 0);
        const auto cameraBeforeImport = v->viewProjection();
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("hill.png")));
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.sceneNodes().size() == 7 && editor.world().height(0, 0) > beforeImport,
              "heightmap imported as a composed scene component");
        auto instanceId = editor.selectedNode();
        auto *instancePanel = win->findChild<QQuickItem *>("terrainInstanceControls");
        check(instancePanel && instancePanel->isVisible(), "selected instance exposes detail controls");
        auto *terrainX = win->findChild<QQuickItem *>("terrainX");
        if (!terrainX)
            terrainX = visualItem(win->contentItem(), "terrainX");
        check(QTest::qWaitFor(
                  [&] {
                      return terrainX && terrainX->isVisible() && terrainX->width() > 40 &&
                             terrainX->height() > 20 && instancePanel->height() > 400;
                  },
                  3000),
              "terrain details lay out actual numeric controls");
        auto *terrainInput = qobject_cast<QQuickItem *>(terrainX->property("contentItem").value<QObject *>());
        check(terrainInput && terrainInput->property("text").toString() == "-1",
              "terrain coordinate control reflects imported origin");
        auto *terrainWindow = terrainInput->window();
        terrainWindow->requestActivate();
        QTest::qWait(80);
        QTest::mouseClick(
            terrainWindow, Qt::LeftButton, Qt::NoModifier,
            terrainInput->mapToScene({terrainInput->width() / 2, terrainInput->height() / 2}).toPoint());
        QTest::qWait(50);
        check(terrainInput->property("selectedText").toString() == "-1",
              "terrain number click selects value");
        QTest::keyClick(terrainWindow, Qt::Key_Delete);
        check(editor.world().heights.instances.size() == 1,
              "Delete in numeric text does not delete instance");
        editor.setSelection(-4, -4, 3, 3);
        QTest::keyClick(terrainWindow, Qt::Key_D, Qt::ControlModifier);
        check(editor.selectionRect().isValid(), "Ctrl+D does not clear selection while typing");
        editor.clearSelection();
        QTest::keyClick(terrainWindow, Qt::Key_1);
        QTest::keyClick(terrainWindow, Qt::Key_6);
        QTest::keyClick(terrainWindow, Qt::Key_Return);
        check(editor.world().heights.instances[0].x == -15, "terrain panel commits typed center");
        editor.undo();
        check(editor.world().heights.instances[0].x == -32, "terrain panel input shares undo history");
        QTest::qWait(60);
        auto terrainScrub = terrainInput->mapToScene({7, terrainInput->height() / 2}).toPoint();
        QTest::mousePress(terrainWindow, Qt::LeftButton, Qt::NoModifier, terrainScrub);
        QTest::mouseMove(terrainWindow, terrainScrub + QPoint(60, 0), 30);
        QTest::mouseMove(terrainWindow, terrainScrub + QPoint(90, 0), 30);
        QTest::mouseRelease(terrainWindow, Qt::LeftButton, Qt::NoModifier, terrainScrub + QPoint(90, 0));
        check(editor.world().heights.instances[0].x != -32 && !editor.terrainEditing(),
              "numeric terrain scrub commits preview transaction");
        const auto scrubbedX = editor.world().heights.instances[0].x;
        check(!terrainInput->hasActiveFocus() && v->hasActiveFocus(),
              "scrub release returns keyboard focus to viewport");
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QTest::qWait(50);
        check(editor.world().heights.instances[0].x == -32 &&
                  terrainInput->property("text").toString() == "-1",
              "numeric scrub is single undo and preserves UI binding");
        QTest::keyClick(win, Qt::Key_Y, Qt::ControlModifier);
        check(editor.world().heights.instances[0].x == scrubbedX, "immediate keyboard redo after scrub");
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QTest::mousePress(terrainWindow, Qt::LeftButton, Qt::NoModifier, terrainScrub);
        QTest::mouseMove(terrainWindow, terrainScrub + QPoint(45, 0), 30);
        QTest::keyClick(terrainWindow, Qt::Key_Escape);
        QTest::mouseRelease(terrainWindow, Qt::LeftButton, Qt::NoModifier, terrainScrub + QPoint(45, 0));
        check(editor.world().heights.instances[0].x == -32 && v->hasActiveFocus() &&
                  !terrainInput->hasActiveFocus() && !editor.terrainEditing(),
              "Escape and subsequent release preserve viewport focus and cancel numeric transaction");
        auto instanceScene = call("world_info", {});
        auto instanceInfo = QJsonDocument::fromJson(
                                instanceScene["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                                .object();
        check(instanceInfo["terrain_instances"].toArray().size() == 1 &&
                  instanceInfo["scene_nodes"].toArray().size() == 7,
              "MCP reports actual terrain instances");
        const int composed = editor.world().height(0, 0);
        result = call("terrain_instance", {{"id", instanceId},
                                           {"changes", QJsonObject{{"enabled", false}}},
                                           {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().height(0, 0) == beforeImport,
              "MCP disables instance");
        result = call("undo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().height(0, 0) == composed,
              "MCP instance edit shared undo");
        const auto validRevision = editor.revision();
        result = call("terrain_instance", {{"id", instanceId},
                                           {"changes", QJsonObject{{"x", 50}, {"width", 0}}},
                                           {"expected_revision", double(validRevision)}});
        check(result["isError"].toBool() && editor.revision() == validRevision &&
                  editor.world().heights.instances[0].x == -32,
              "invalid MCP instance batch is atomic");
        const auto preSmart = editor.world().heights.instances;
        result = call("terrain_instance",
                      {{"id", instanceId},
                       {"changes", QJsonObject{{"blend", 4}, {"blendSmoothness", 24}, {"blendEdge", 8}}},
                       {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().heights.instances[0].blend == 4 &&
                  editor.terrainDetails()["blendSmoothness"].toInt() == 24 &&
                  editor.worldInfo()["terrain_instances"].toArray()[0].toObject()["blendEdge"].toInt() == 8,
              "real stdio MCP edits and reports smart parameters through shared state");
        const auto smartRevision = editor.revision();
        result = call("terrain_instance", {{"id", instanceId},
                                           {"changes", QJsonObject{{"x", 999}, {"blendSmoothness", 129}}},
                                           {"expected_revision", double(smartRevision)}});
        check(result["isError"].toBool() && editor.revision() == smartRevision &&
                  editor.world().heights.instances[0].x == -32,
              "invalid smart MCP batch changes nothing");
        result = call("terrain_instance", {{"id", instanceId},
                                           {"changes", QJsonObject{{"blendEdge", 0}}},
                                           {"expected_revision", double(validRevision)}});
        check(result["isError"].toBool() && editor.revision() == smartRevision,
              "stale smart request rejected");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("smart.wbench"))), "GUI saves smart parameters");
        wb::World smartSaved;
        QString smartError;
        check(wb::loadProject(tmp.filePath("smart.wbench"), smartSaved, smartError) &&
                  smartSaved.heights.instances == editor.world().heights.instances,
              "GUI saved smart project preserves parameters");
        result = call("undo", {{"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().heights.instances == preSmart,
              "smart MCP transaction undoes as one command");
        editor.redo();
        check(editor.world().heights.instances[0].blend == 4, "smart transaction redo");
        editor.undo();
        editor.setTerrainOption("x", -16);
        editor.undo();
        check(editor.world().heights.instances[0].x == -32, "GUI transform undo");
        editor.redo();
        check(editor.world().heights.instances[0].x == -16, "GUI transform redo");
        QImage maskImage(2, 2, QImage::Format_Grayscale8);
        maskImage.fill(0);
        maskImage.save(tmp.filePath("mask.png"));
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("mask.png")), true);
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.world().height(0, 0) == beforeImport,
              "black mask disables terrain contribution");
        editor.clearTerrainMask();
        check(editor.world().height(0, 0) > beforeImport, "remove mask restores contribution");
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("hill.png")));
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.sceneNodes().size() == 8,
              "multiple instances in outliner");
        check(v->viewProjection() == cameraBeforeImport, "heightmap imports preserve the current camera");
        const auto topId = editor.selectedNode();
        editor.moveTerrain(-1);
        check(editor.world().heights.instances[0].id == topId, "instance ordering updates");
        editor.undo();
        check(editor.world().heights.instances[1].id == topId, "instance ordering undo");
        editor.removeTerrain();
        check(editor.sceneNodes().size() == 7, "remove instance");
        editor.undo();
        check(editor.sceneNodes().size() == 8, "undo removal restores instance");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("terrain.wbench"))), "save instance project");
        QFile::remove(tmp.filePath("hill.png"));
        QFile::remove(tmp.filePath("mask.png"));
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("terrain.wbench"))) &&
                  editor.sceneNodes().size() == 8,
              "embedded terrain reopens without PNGs");
        editor.setSelectedNode(topId);
        editor.setTool(-1);
        editor.focusTerrain();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "instance preview rendered");
        QTest::qWait(150);
        check(win->grabWindow().save("test-output/terrain-0.13.0.png"), "capture terrain instance controls");
        auto *detailScroll = win->findChild<QQuickItem *>("sceneDetailsScroll");
        auto *detailFlickable = detailScroll->property("contentItem").value<QObject *>();
        auto *removeInstance = win->findChild<QQuickItem *>("removeTerrainButton");
        check(detailFlickable && removeInstance, "terrain panel exposes scrollable lower actions");
        detailFlickable->setProperty("contentY", detailFlickable->property("contentHeight").toDouble() -
                                                     detailFlickable->property("height").toDouble());
        QTest::qWait(100);
        const auto removePoint = removeInstance->mapToItem(detailScroll, {0, removeInstance->height() / 2});
        check(removePoint.y() >= 0 && removePoint.y() < detailScroll->height(),
              "scrolling reveals terrain mask and removal controls");
        check(win->grabWindow().save("test-output/terrain-actions-0.13.0.png"),
              "capture lower terrain controls");
        auto *blendControl = win->findChild<QQuickItem *>("terrainBlend");
        auto *smoothControl = win->findChild<QQuickItem *>("terrainBlendSmoothness");
        auto *edgeControl = win->findChild<QQuickItem *>("terrainBlendEdge");
        check(blendControl && smoothControl && edgeControl && !smoothControl->isVisible(),
              "smart settings hidden in existing blend modes");
        detailFlickable->setProperty("contentY", 0);
        QTest::qWait(50);
        auto scrollTo = [&](QQuickItem *item) {
            const double current = detailFlickable->property("contentY").toDouble();
            const double target = current + item->mapToItem(detailScroll, {0, 0}).y() - 60;
            detailFlickable->setProperty("contentY", std::max(0., target));
            QTest::qWait(80);
        };
        scrollTo(blendControl);
        blendControl->forceActiveFocus();
        QTest::keyClick(win, Qt::Key_End);
        check(QTest::qWaitFor([&] { return editor.terrainDetails()["blend"].toInt() == 4; }, 3000),
              "blend selector keyboard chooses smart mode");
        check(smoothControl->isVisible() && edgeControl->isVisible(), "smart controls appear in details");
        scrollTo(smoothControl);
        auto *smoothInput =
            qobject_cast<QQuickItem *>(smoothControl->property("contentItem").value<QObject *>());
        check(smoothInput, "smart smoothing input");
        smoothInput->forceActiveFocus();
        QTest::keyClick(win, Qt::Key_A, Qt::ControlModifier);
        for (auto key : {Qt::Key_6, Qt::Key_4, Qt::Key_Slash, Qt::Key_2})
            QTest::keyClick(win, key);
        QTest::keyClick(win, Qt::Key_Return);
        check(editor.terrainDetails()["blendSmoothness"].toInt() == 32,
              "smart smoothing supports typed arithmetic");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "smart UI preview settles");
        win->grabWindow().save("test-output/smart-controls-0.13.0.png");
        editor.undo();
        editor.undo();
        check(editor.terrainDetails()["blend"].toInt() == 1 && !smoothControl->isVisible(),
              "smart GUI changes undo to previous mode without changing other parameters");
        detailFlickable->setProperty("contentY", 0);
        v->resetCamera();
        editor.setTool(-1);
        v->forceActiveFocus();
        QTest::qWait(80);
        QElapsedTimer instanceLatency;
        instanceLatency.start();
        editor.setNodeVisible(topId, false);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000),
              "visibility optimized rebuild settles");
        const auto visibilityMs = instanceLatency.elapsed();
        check(editor.scene()->updatedChunks < 64, "instance toggle rebuilds only its footprint");
        editor.undo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "visibility undo settles");
        auto originalInstance = editor.world().heights.instances.back();
        instanceLatency.restart();
        editor.beginTerrainEdit();
        const auto beforePreviewSerial = editor.scene()->revision;
        int liveSnapshots = 0;
        uint64_t lastSerial = beforePreviewSerial;
        for (int n = 0; n < 180; ++n) {
            editor.setTerrainOption("x", originalInstance.x + (n % 24) + 1);
            QTest::qWait(16);
            if (editor.scene()->provisional && editor.scene()->revision > lastSerial) {
                ++liveSnapshots;
                lastSerial = editor.scene()->revision;
            }
        }
        check(liveSnapshots >= 2, "continuous moving input publishes previews without waiting for a pause");
        check(QTest::qWaitFor(
                  [&] {
                      return editor.scene()->provisional && editor.scene()->revision > beforePreviewSerial &&
                             editor.scene()->sourceRevision == editor.revision();
                  },
                  30000),
              "active transform publishes complete provisional snapshots without mixed preview tiles");
        editor.endTerrainEdit();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000),
              "coalesced exact transform rebuild settles");
        check(!editor.scene()->provisional, "released transform replaces provisional mesh with exact mesh");
        check(std::none_of(editor.scene()->farTiles.begin(), editor.scene()->farTiles.end(),
                           [](const auto &tile) { return tile.mesh->provisional; }),
              "release rebuilds every temporary tile at final precision");
        std::cout << "Transform interaction 180 updates, " << liveSnapshots << " live snapshots in "
                  << instanceLatency.elapsed() << " ms, final build " << editor.scene()->buildMilliseconds
                  << " ms; visibility " << visibilityMs << " ms\n";
        editor.undo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "interaction undo settles");
        auto overlay = v->gizmoOverlay();
        check(overlay["active"].toBool() && overlay["markers"].toList().size() == 2,
              "projected instance markers and gizmo visible");
        auto marker = overlay["markers"].toList().last().toMap();
        editor.setSelectedNode("terrain-base-1");
        auto iconPoint = v->mapToScene({marker["x"].toDouble(), marker["y"].toDouble()}).toPoint();
        QTest::mousePress(win, Qt::RightButton, Qt::NoModifier, iconPoint);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, iconPoint);
        check(editor.selectedNode() == "terrain-base-1" && !v->gizmoDragging(),
              "combined navigation buttons bypass instance markers");
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, iconPoint);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, iconPoint);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, iconPoint);
        check(editor.selectedNode() == topId, "click viewport icon selects the instance");
        auto anchorScreen = [&] {
            const auto data = v->gizmoOverlay();
            return QPointF(data["cx"].toDouble(), data["cy"].toDouble());
        };
        const auto fixedAnchor = anchorScreen();
        const auto selectedMarker = v->gizmoOverlay()["markers"].toList().last().toMap();
        check(QLineF(fixedAnchor, {selectedMarker["x"].toDouble(), selectedMarker["y"].toDouble()}).length() <
                  .01,
              "instance badge and transform center coincide");
        editor.setTerrainOption("scaleY", 180);
        check(QLineF(fixedAnchor, anchorScreen()).length() < .01,
              "anchor ignores raised surface from scaling");
        editor.setTerrainOption("enabled", false);
        check(QLineF(fixedAnchor, anchorScreen()).length() < .01,
              "anchor ignores visibility and combined terrain");
        editor.undo();
        editor.undo();
        const auto centerStart = v->mapToScene(fixedAnchor).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, centerStart);
        check(v->gizmoDragging() && v->gizmoOverlay()["axis"].toInt() == 3,
              "selected icon does not intercept shared white center");
        QTest::mouseMove(win, centerStart + QPoint(25, 0), 40);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, centerStart + QPoint(25, 0));
        check(editor.world().heights.instances.back().x != originalInstance.x ||
                  editor.world().heights.instances.back().z != originalInstance.z,
              "shared white center translates instance");
        editor.undo();
        auto dragAxis = [&](int mode, int axis, bool cancel) {
            v->setGizmoMode(mode);
            auto data = v->gizmoOverlay();
            auto pts = data["axes"].toList()[axis].toMap()["points"].toList();
            QPointF localStart, localEnd;
            if (mode == 1) {
                auto a = pts[8].toMap(), b = pts[13].toMap();
                localStart = {a["x"].toDouble(), a["y"].toDouble()};
                localEnd = {b["x"].toDouble(), b["y"].toDouble()};
            } else {
                auto a = pts[0].toMap(), b = pts[1].toMap();
                QPointF start(a["x"].toDouble(), a["y"].toDouble()),
                    end(b["x"].toDouble(), b["y"].toDouble());
                localStart = start + (end - start) * .75;
                localEnd = localStart + (end - start) * .35;
            }
            auto start = v->mapToScene(localStart).toPoint(), end = v->mapToScene(localEnd).toPoint();
            QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, start);
            check(v->gizmoDragging(), "gizmo handle captures drag");
            QTest::mouseMove(win, end, 40);
            check(!v->gizmoOverlay()["readout"].toString().isEmpty(),
                  "gizmo reports actual transform amount");
            if (mode == 0 && !cancel) {
                auto moved = v->gizmoOverlay();
                check(QLineF(QPointF(data["cx"].toDouble(), data["cy"].toDouble()),
                             QPointF(moved["cx"].toDouble(), moved["cy"].toDouble()))
                              .length() > 1,
                      "gizmo display follows translation during drag");
                check(!moved["readout"].toString().contains("Y") &&
                          !moved["readout"].toString().contains("Z") &&
                          !moved["readout"].toString().contains("Ctrl"),
                      "single-axis readout stays compact");
                win->grabWindow().save("test-output/gizmo-active-0.13.0.png");
            }
            if (mode == 1) {
                check(v->gizmoOverlay()["sector"].toList().size() > 3 &&
                          !v->gizmoOverlay()["ticks"].toList().isEmpty(),
                      "rotation gesture exposes sector protractor");
                win->grabWindow().save("test-output/rotation-sector-0.13.0.png");
            }
            if (cancel)
                QTest::keyClick(win, Qt::Key_Escape);
            QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, end);
        };
        QTest::keyClick(win, Qt::Key_E);
        check(v->gizmoMode() == 1, "E selects rotate without flying");
        QTest::keyClick(win, Qt::Key_R);
        check(v->gizmoMode() == 2, "R selects scale");
        QTest::keyClick(win, Qt::Key_W);
        check(v->gizmoMode() == 0, "W selects translate without RMB");
        dragAxis(0, 0, false);
        check(editor.world().heights.instances.back().x != originalInstance.x,
              "axis drag translates terrain");
        editor.undo();
        check(editor.world().heights.instances.back() == originalInstance,
              "entire translate drag is one undo");
        dragAxis(2, 0, false);
        check(editor.world().heights.instances.back().scaleX != 100, "axis drag scales terrain");
        editor.undo();
        check(editor.world().heights.instances.back() == originalInstance, "scale drag undo");
        dragAxis(1, 1, false);
        check(editor.world().heights.instances.back() != originalInstance, "ring drag rotates terrain");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "rotated terrain preview settles");
        win->grabWindow().save("test-output/rotate-0.13.0.png");
        editor.undo();
        check(editor.world().heights.instances.back() == originalInstance, "rotation drag undo");
        dragAxis(0, 0, true);
        check(editor.world().heights.instances.back() == originalInstance && !editor.terrainEditing(),
              "Esc cancels transform transaction");
        v->setGizmoMode(0);
        auto plane = v->gizmoOverlay()["planes"].toList()[1].toMap()["points"].toList();
        QPointF planeCenter;
        for (auto point : plane) {
            auto p = point.toMap();
            planeCenter += QPointF(p["x"].toDouble(), p["y"].toDouble()) / 4.;
        }
        auto planeStart = v->mapToScene(planeCenter).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, planeStart);
        check(v->gizmoDragging() && v->gizmoOverlay()["axis"].toInt() == 5, "XZ plane handle is selectable");
        QTest::mouseMove(win, planeStart + QPoint(18, 12), 30);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, planeStart + QPoint(18, 12));
        auto planeMoved = editor.world().heights.instances.back();
        check(planeMoved.y == originalInstance.y &&
                  (planeMoved.x != originalInstance.x || planeMoved.z != originalInstance.z),
              "plane handle constrains translation to XZ");
        editor.undo();
        editor.setTerrainOption("rotationY", 90);
        v->setGizmoMode(2);
        auto directionOf = [](QVariantMap data, int axis) {
            const auto points = data["axes"].toList()[axis].toMap()["points"].toList();
            auto first = points.front().toMap(), last = points.back().toMap();
            return QPointF(last["x"].toDouble() - first["x"].toDouble(),
                           last["y"].toDouble() - first["y"].toDouble());
        };
        const auto localZ = directionOf(v->gizmoOverlay(), 2);
        v->setGizmoMode(0);
        const auto worldX = directionOf(v->gizmoOverlay(), 0);
        check(QPointF::dotProduct(localZ, worldX) /
                      (QLineF({}, localZ).length() * QLineF({}, worldX).length()) >
                  .999,
              "rotated scale Z handle follows actual local axis");
        dragAxis(2, 2, false);
        check(editor.world().heights.instances.back().scaleZ != 100 &&
                  editor.world().heights.instances.back().scaleX == 100,
              "rotated Z scale handle modifies only local Z");
        editor.undo();
        editor.undo();
        editor.setTerrainOptions({{"scaleX", 80}, {"scaleZ", 160}});
        v->setGizmoMode(2);
        auto scalePlane = v->gizmoOverlay()["planes"].toList()[1].toMap()["points"].toList();
        QPointF scaleCenter;
        for (auto point : scalePlane) {
            auto q = point.toMap();
            scaleCenter += QPointF(q["x"].toDouble(), q["y"].toDouble()) / 4.;
        }
        auto scaleStart = v->mapToScene(scaleCenter).toPoint();
        const auto scaleDrag =
            directionOf(v->gizmoOverlay(), 0) * .25 + directionOf(v->gizmoOverlay(), 2) * .3;
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, scaleStart);
        check(v->gizmoDragging() && v->gizmoOverlay()["axis"].toInt() == 5,
              "scale XZ plane captures gesture");
        QTest::mouseMove(win, scaleStart + scaleDrag.toPoint(), 40);
        const auto pairScaled = editor.world().heights.instances.back();
        check(pairScaled.scaleX != 80 && pairScaled.scaleZ != 160 && pairScaled.scaleY == 100 &&
                  std::abs(pairScaled.scaleZ - pairScaled.scaleX * 2) <= 1,
              "plane preserves unequal starting ratio and leaves third axis unchanged");
        win->grabWindow().save("test-output/scale-plane-0.13.0.png");
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, scaleStart + scaleDrag.toPoint());
        editor.undo();
        v->setSnapEnabled(true);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, scaleStart);
        QTest::mouseMove(win, scaleStart + scaleDrag.toPoint(), 40);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, scaleStart + scaleDrag.toPoint());
        const auto snappedPair = editor.world().heights.instances.back();
        check(snappedPair.scaleX != 80 && snappedPair.scaleZ == snappedPair.scaleX * 2 &&
                  snappedPair.scaleY == 100,
              "group snapping quantizes shared multiplier without distorting ratio");
        editor.undo();
        v->setSnapEnabled(false);
        editor.undo();
        auto *magnet = visualItem(win->contentItem(), "snapToggle");
        check(magnet, "magnet toolbar button present");
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier,
                          magnet->mapToScene({magnet->width() / 2, magnet->height() / 2}).toPoint());
        check(v->snapEnabled(), "magnet enables persistent snapping");
        dragAxis(0, 0, false);
        check((editor.world().heights.instances.back().x - originalInstance.x) % 4 == 0,
              "toolbar snapping works without Ctrl");
        editor.undo();
        v->setSnapEnabled(false);
        v->forceActiveFocus();
        QTest::keyClick(win, Qt::Key_Delete);
        std::cout << "Delete instance count " << editor.world().heights.instances.size()
                  << " selected=" << editor.selectedNode().toStdString() << " top=" << topId.toStdString()
                  << std::endl;
        check(editor.world().heights.instances.size() == 1, "Delete removes selected instance");
        editor.undo();
        check(editor.world().heights.instances.size() == 2, "Delete participates in undo");
        editor.setSelectedNode(topId);
        check(QTest::qWaitFor([&] { return visualItem(win->contentItem(), "eye_" + topId); }, 2000),
              "restored outliner delegate becomes available");
        auto *eye = visualItem(win->contentItem(), "eye_" + topId);
        check(eye, "outliner has visibility eye");
        QTest::mouseClick(eye->window(), Qt::LeftButton, Qt::NoModifier,
                          eye->mapToScene({eye->width() / 2, eye->height() / 2}).toPoint());
        check(!editor.world().heights.instances.back().enabled, "eye hides instance generation");
        editor.undo();
        result = call("sculpt_layer", {{"action", "add"}, {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().heights.layers.size() == 2,
              "MCP creates real sculpt layer");
        auto sculptId = editor.world().heights.activeLayer;
        const auto layerSelectionRevision = editor.revision();
        editor.setActiveSculptLayer("terrain-base-1");
        result = call("sculpt", {{"position", QJsonArray{0, editor.world().height(0, 0), 0}},
                                 {"tool", 0},
                                 {"radius", 1},
                                 {"strength", 4},
                                 {"expected_revision", double(layerSelectionRevision)}});
        check(result["isError"].toBool() && editor.revision() != layerSelectionRevision,
              "changing active layer invalidates pending MCP sculpt");
        editor.setActiveSculptLayer(sculptId);
        const int baseComposed = editor.world().height(0, 0);
        result = call("sculpt", {{"position", QJsonArray{0, baseComposed, 0}},
                                 {"tool", 0},
                                 {"radius", 1},
                                 {"strength", 4},
                                 {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().height(0, 0) == baseComposed + 4,
              "MCP sculpts relative height into active layer");
        editor.setNodeVisible(instanceId, false);
        editor.setNodeVisible(topId, false);
        check(editor.world().height(0, 0) == 28,
              "two hidden instances remove contributions while plus four remains");
        result = call("sculpt_layer", {{"action", "visible"},
                                       {"id", sculptId},
                                       {"enabled", false},
                                       {"expected_revision", double(editor.revision())}});
        check(!result["isError"].toBool() && editor.world().height(0, 0) == 24,
              "hiding sculpt layer removes only its delta");
        auto hiddenRevision = editor.revision();
        result = call("sculpt", {{"position", QJsonArray{0, 24, 0}},
                                 {"tool", 0},
                                 {"radius", 1},
                                 {"strength", 4},
                                 {"expected_revision", double(hiddenRevision)}});
        check(result["isError"].toBool() && editor.revision() == hiddenRevision,
              "MCP refuses sculpt on hidden layer");
        editor.setNodeVisible(sculptId, true);
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("layers.wbench"))) &&
                  editor.open(QUrl::fromLocalFile(tmp.filePath("layers.wbench"))) &&
                  editor.world().height(0, 0) == 28 && editor.world().heights.activeLayer == sculptId,
              "GUI layer project reopens with relative state");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "layer preview ready");
        win->grabWindow().save("test-output/layers-0.13.0.png");
        // Reproduce the reported second-image failure through the actual asynchronous importer.
        check(editor.createWorld(16384, 42, 24, 0, -60, true), "create large import/MCP world");
        const auto importCamera = v->cameraText();
        QImage largePng(2048, 2048, QImage::Format_Grayscale16);
        for (int imageId = 0; imageId < 2; ++imageId) {
            for (int z = 0; z < 2048; ++z) {
                auto row = reinterpret_cast<quint16 *>(largePng.scanLine(z));
                for (int x = 0; x < 2048; ++x)
                    row[x] = quint16((x + z + imageId * 7000) % 65536);
            }
            const auto file = tmp.filePath(QString("large-%1.png").arg(imageId));
            check(largePng.save(file), "save distinct 2K PNG fixture");
            editor.importHeightmap(QUrl::fromLocalFile(file));
            check(QTest::qWaitFor([&] { return !editor.assetBusy(); }, 15000) &&
                      editor.world().heights.instances.size() == size_t(imageId + 1),
                  "sequential distinct 2K imports both accepted");
        }
        check(v->cameraText() == importCamera, "large image imports preserve camera");
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("large-0.png")));
        check(QTest::qWaitFor([&] { return !editor.assetBusy(); }, 15000) &&
                  editor.world().heights.instances.size() == 3 &&
                  editor.world().heights.instances[0].image == editor.world().heights.instances[2].image,
              "identical imported content shares immutable asset");
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("large-0.png")), true);
        check(QTest::qWaitFor([&] { return !editor.assetBusy(); }, 15000) &&
                  editor.world().heights.instances[2].mask == editor.world().heights.instances[0].image,
              "imported mask shares identical existing image");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("large-images.wbench"))) &&
                  editor.open(QUrl::fromLocalFile(tmp.filePath("large-images.wbench"))) &&
                  editor.world().heights.instances.size() == 3 &&
                  editor.world().heights.instances[0].image == editor.world().heights.instances[2].image,
              "GUI multiple large image project saves and reopens");
        auto toolData = [&](const QJsonObject &response) {
            check(!response["isError"].toBool(), "MCP region response succeeds");
            return QJsonDocument::fromJson(
                       response["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                .object();
        };
        const auto mcpBeforeRegion = editor.world().heights;
        const auto regionRevision = editor.revision();
        const auto selectionBeforeRegion = editor.selection();
        const bool undoBeforeRegion = editor.canUndo(), redoBeforeRegion = editor.canRedo();
        auto startArgs = QJsonObject{{"min", QJsonArray{-512, -512}},
                                     {"max", QJsonArray{511, 511}},
                                     {"offset", 4},
                                     {"expected_revision", double(regionRevision)}};
        auto badRegion = startArgs;
        badRegion["expected_revision"] = double(regionRevision - 1);
        check(call("height_region", badRegion)["isError"].toBool(), "stale region start rejected");
        badRegion = startArgs;
        badRegion["min"] = QJsonArray{-999999, -512};
        check(call("height_region", badRegion)["isError"].toBool(), "invalid region bounds rejected");
        const auto running = toolData(call("height_region", startArgs));
        const auto cancelledId = running["job_id"].toString();
        check(!cancelledId.isEmpty() && running["state"] == "running", "stdio returns asynchronous job ID");
        const auto cancellation =
            toolData(call("region_job", {{"job_id", cancelledId}, {"action", "cancel"}}));
        check(cancellation["state"] == "cancelling" || cancellation["state"] == "cancelled",
              "real stdio rapid cancel reaches pending job");
        check(QTest::qWaitFor([&] { return !editor.regionBusy(); }, 15000), "MCP cancelled worker settles");
        auto ended = toolData(call("region_job", {{"job_id", cancelledId}, {"action", "status"}}));
        check(ended["state"] == "cancelled" && ended["changed_columns"] == 0 &&
                  editor.revision() == regionRevision && editor.world().heights == mcpBeforeRegion &&
                  editor.canUndo() == undoBeforeRegion && editor.canRedo() == redoBeforeRegion,
              "MCP cancellation leaves values revision and history intact");
        std::cout << "Real stdio region: cancelled; unchanged revision " << regionRevision
                  << "; no committed columns\n";
        const auto runningAgain = toolData(call("height_region", startArgs));
        const auto completedId = runningAgain["job_id"].toString();
        check(call("region_job", {{"job_id", cancelledId}, {"action", "cancel"}})["isError"].toBool(),
              "stale job ID cannot cancel newer job");
        check(call("undo", {{"expected_revision", double(regionRevision)}})["isError"].toBool(),
              "busy region rejects competing writes");
        check(QTest::qWaitFor([&] { return !editor.regionBusy(); }, 15000), "MCP region completes");
        ended = toolData(call("region_job", {{"job_id", completedId}, {"action", "cancel"}}));
        check(ended["state"] == "completed" && ended["changed_columns"] == 1048576 &&
                  editor.revision() == regionRevision + 1 && editor.selection() == selectionBeforeRegion,
              "completed job cannot be retroactively cancelled or change GUI selection");
        check(!call("undo", {{"expected_revision", double(editor.revision())}})["isError"].toBool() &&
                  editor.world().heights == mcpBeforeRegion,
              "MCP region undo restores entire snapshot");
        check(!call("redo", {{"expected_revision", double(editor.revision())}})["isError"].toBool() &&
                  editor.world().height(0, 0) == mcpBeforeRegion[editor.world().column(0, 0)] + 4,
              "MCP region redo uses shared snapshot");
        check(editor.createWorld(128, 42, 24, 0, -60, true), "replace completed job world");
        check(call("region_job", {{"job_id", completedId}, {"action", "status"}})["isError"].toBool(),
              "new project expires old task ID");
        editor.setMcpEnabled(false);
        check(!editor.mcpEnabled(), "MCP disabled");
        bridge.closeWriteChannel();
        check(QTest::qWaitFor([&] { return bridge.state() == QProcess::NotRunning; }, 5000),
              "MCP bridge exits on stdin EOF");
        // Creation and local history invalidation, with the real renderer.
        check(editor.createWorld(128, 42, 24, 0, -60, true), "create configured flat world");
        check(editor.world().seed == 42 && editor.world().seaLevel == -60 &&
                  editor.world().height(0, 0) == 24 && editor.world().overrides.empty(),
              "creation parameters applied");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "flat world rendered");
        const auto fullBuild = editor.scene()->buildMilliseconds;
        editor.setTool(0);
        editor.setRadius(1);
        editor.beginStroke();
        editor.stamp({true, -24, 24, -24, {-23.5f, 25, -23.5f}, {0, 1, 0}, wb::Grass}, false);
        editor.endStroke();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "small stroke rendered");
        auto localScene = editor.scene();
        editor.undo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "local undo rendered");
        int sameChunks = 0;
        for (size_t i = 0; i < localScene->chunks.size(); ++i)
            if (localScene->chunks[i] == editor.scene()->chunks[i])
                ++sameChunks;
        check(editor.scene()->updatedChunks == 1 && sameChunks == 63,
              "undo keeps 63 of 64 meshes instead of rebuilding world");
        std::cout << "Mesh full " << fullBuild << " ms / undo " << editor.scene()->buildMilliseconds
                  << " ms (1 of 64 chunks)\n";
        editor.redo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000) &&
                  editor.scene()->updatedChunks == 1,
              "redo updates one chunk");
        editor.setTool(1);
        editor.setBrushOption("fillHoles", false);
        editor.setRadius(9);
        editor.setBrushOption("decay", .75);
        editor.setTool(0);
        editor.setRadius(3);
        editor.setBrushOption("decay", .25);
        editor.setBrushOption("shape", 1);
        editor.setTool(1);
        check(editor.radius() == 9 && editor.brushOptions()["decay"].toDouble() == .75 &&
                  editor.brushShape() == 0,
              "brush settings remain independent between tools");
        {
            Editor another;
            another.setTool(1);
            check(another.radius() == 9 && another.brushOptions()["decay"].toDouble() == .75,
                  "per-tool settings persist");
        }
        editor.setTool(0);
        editor.setRadius(3);
        editor.setBrushOption("shape", 0);
        v->resetCamera();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "stationary brush fixture ready");
        // Programmatic editor operations above can leave the desktop window
        // inactive. A held brush intentionally stops when it loses activation.
        win->requestActivate();
        check(QTest::qWaitForWindowActive(win), "held brush test window active");
        v->forceActiveFocus();
        point = v->mapToScene(QPointF(v->width() * .5, v->height() * .6)).toPoint();
        QTest::mouseMove(win, point);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, point);
        const auto anchored = v->hit();
        QTest::qWait(550);
        const auto held = v->hit();
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, point);
        if (!anchored.valid || held.x != anchored.x || held.z != anchored.z || held.y <= anchored.y)
            std::cerr << "Held brush active=" << win->isActive() << " focus=" << v->hasActiveFocus()
                      << " height=" << anchored.y << " -> " << held.y << std::endl;
        check(anchored.valid && held.x == anchored.x && held.z == anchored.z && held.y > anchored.y,
              "stationary held brush raises fixed XZ instead of repicking slope");
        editor.undo();
        editor.setRadius(12);
        editor.setBrushOption("decay", .5);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "brush preview ready");
        QTest::mouseMove(win, point + QPoint(1, 0));
        QTest::qWait(120);
        check(win->grabWindow().save("test-output/brush-decay-0.13.0.png"), "capture both decay rings");
        editor.setSelection(-8, -8, 7, 7);
        v->forceActiveFocus();
        QTest::keyClick(win, Qt::Key_D, Qt::ControlModifier);
        check(!editor.selectionRect().isValid(), "Ctrl+D clears selection");
        editor.setSelection(-8, -8, 7, 7);
        const auto beforeRegion = editor.world();
        check(editor.adjustSelectedHeight(6) && editor.assetBusy(), "region operation starts asynchronously");
        check(!editor.adjustSelectedHeight(2), "concurrent region request rejected");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000),
              "region operation settles");
        check(editor.world().height(0, 0) == beforeRegion.height(0, 0) + 6 &&
                  editor.world().height(10, 10) == beforeRegion.height(10, 10) &&
                  editor.world().overrides == beforeRegion.overrides,
              "batch changes only selected columns and preserves independent voxels");
        editor.undo();
        check(editor.world().heights == beforeRegion.heights, "batch is one undo");
        editor.redo();
        check(editor.world().height(0, 0) == beforeRegion.height(0, 0) + 6, "batch redo");
        const std::weak_ptr<wb::HeightDeltas> publishedRegion = editor.world().heights.layers[0].deltas;
        editor.undo();
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("drop-source.wbench"))),
              "save native drop fixture");
        const QUrl dropUrl = QUrl::fromLocalFile(tmp.filePath("drop-source.wbench"));
        check(editor.acceptsProjectDrop({dropUrl}) && !editor.acceptsProjectDrop({dropUrl, dropUrl}) &&
                  !editor.acceptsProjectDrop({QUrl("https://example.com/x.wbench")}),
              "project drop accepts one existing local project only");
        check(editor.createWorld(64, 11, 10, 0, -60, true), "prepare different drop destination");
        check(QTest::qWaitFor([&] { return !editor.meshBusy() && publishedRegion.expired(); }, 15000),
              "opening a new world releases discarded region snapshot and completed future result");
        auto sendDrop = [&] {
            QMimeData mime;
            mime.setUrls({dropUrl});
            QPoint pos = win->contentItem()->mapToScene({win->width() * .45, win->height() * .45}).toPoint();
            QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(win, &enter);
            check(enter.isAccepted(), "native drag enter accepts wbench");
            QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(win, &drop);
            check(drop.isAccepted(), "native drop accepted");
        };
        sendDrop();
        check(editor.projectPath() == dropUrl.toLocalFile() && editor.world().width == 128,
              "dropping opens the project through UI");
        editor.beginStroke();
        editor.stamp({true, 0, editor.world().height(0, 0), 0, {.5f, 25, .5f}, {0, 1, 0}, wb::Grass}, false);
        editor.endStroke();
        const auto dirtyDropWorld = editor.world().heights;
        sendDrop();
        check(win->property("pendingAction").toString() == "drop" && editor.world().heights == dirtyDropWorld,
              "dirty project drop waits for save decision");
        auto *unsaved = win->findChild<QObject *>("unsavedDialog");
        check(unsaved && QMetaObject::invokeMethod(unsaved, "reject"), "cancel native save dialog");
        check(win->property("pendingAction").toString().isEmpty() && editor.world().heights == dirtyDropWorld,
              "cancel clears pending drop and preserves unsaved edits");
        win->requestActivate();
        check(QTest::qWaitForWindowActive(win), "main window active after native prompt closes");
        editor.undo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "drop and undo settled");
        const auto beforeSelection = editor.world().heights;
        const auto selectionRevision = editor.revision();
        editor.setTool(6);
        QTest::qWait(60);
        auto *selectionPanel = win->findChild<QQuickItem *>("selectionSettings");
        check(selectionPanel && selectionPanel->isVisible() &&
                  !win->findChild<QQuickItem *>("brushToolControls")->isVisible(),
              "selection tool opens dedicated left controls");
        point = v->mapToScene(QPointF(v->width() * .40, v->height() * .55)).toPoint();
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(win, point + QPoint(100, 60), 30);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, point + QPoint(100, 60));
        if (editor.selectionRect().width() <= 1 || editor.selectionRect().height() <= 1 ||
            editor.revision() != selectionRevision) {
            std::cerr << "Selection diagnostic: tool=" << editor.tool()
                      << " rect=" << editor.selectionRect().x() << "," << editor.selectionRect().y() << ","
                      << editor.selectionRect().width() << "," << editor.selectionRect().height()
                      << " hit=" << v->hit().valid << " revision=" << editor.revision()
                      << " expected=" << selectionRevision << " windowActive=" << win->isActive()
                      << " dims=" << v->width() << "," << v->height() << "\n";
            win->grabWindow().save("test-output/selection-failure-0.13.0.png");
        }
        check(editor.selectionRect().width() > 1 && editor.selectionRect().height() > 1 &&
                  editor.world().heights == beforeSelection && editor.revision() == selectionRevision,
              "actual mouse drag selects rectangle without changing terrain");
        QTest::qWait(120);
        check(win->grabWindow().save("test-output/selection-0.13.0.png"), "capture selection boundary");
        editor.clearSelection();
        editor.setTool(1);
        QTest::qWait(30);
        auto *depthControls = win->findChild<QQuickItem *>("fillDepthControls");
        check(depthControls && !depthControls->isVisible(), "fill depth hidden unless enabled");
        editor.setBrushOption("fillHoles", true);
        QTest::qWait(30);
        check(depthControls->isVisible(), "fill depth shown when enabled");
        editor.setBrushOption("fillHoles", false);
        QDir().mkpath(tmp.filePath("jar-folder"));
        QDir().mkpath(tmp.filePath("pack-folder"));
        editor.rememberDialogFolder("baseResource", QUrl::fromLocalFile(tmp.filePath("jar-folder/game.jar")));
        editor.rememberDialogFolder("overlayResource",
                                    QUrl::fromLocalFile(tmp.filePath("pack-folder/pack.zip")));
        check(editor.dialogFolder("baseResource") != editor.dialogFolder("overlayResource"),
              "separate picker histories");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("without-resources.wbench"))),
              "save project before default resources");
        const QString resource = tmp.filePath("texture-pack.zip");
        {
            QImage tile(16, 16, QImage::Format_RGBA8888);
            tile.fill(Qt::cyan);
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            tile.save(&buffer, "PNG");
            QZipWriter zip(resource);
            zip.addFile("assets/minecraft/textures/block/stone.png", png);
            zip.close();
        }
        QSignalSpy progressSpy(&editor, &Editor::assetProgressChanged);
        int progressLast = 0;
        bool progressMonotonic = true;
        auto progressConnection = QObject::connect(&editor, &Editor::assetProgressChanged, [&] {
            progressMonotonic &= editor.assetProgress() >= progressLast;
            progressLast = editor.assetProgress();
        });
        editor.importResource(QUrl::fromLocalFile(resource), false);
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 10000),
              "async resource import settles");
        QObject::disconnect(progressConnection);
        check(progressSpy.size() > 3 && progressMonotonic && editor.assetProgress() == 100 &&
                  !editor.assetLog().isEmpty(),
              "resource progress and live logs");
        check(editor.defaultResourceSources() == QStringList{resource},
              "successful manual import becomes default");
        check(QTest::qWaitFor(
                  [&] {
                      return !win->findChild<QObject *>("editorSettingsDialog")->property("visible").toBool();
                  },
                  3000),
              "automatic resource progress window closes after success and mesh completion");
        const auto cached = editor.world().assets;
        editor.newWorld();
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.world().assets == cached && editor.assetLog().contains("复用"),
              "new world reuses parsed default resource");
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("without-resources.wbench"))),
              "open project without own resources");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.world().assets == cached && !editor.modified(),
              "resource-free project inherits default without false dirty state");
        {
            Editor restarted;
            restarted.initializeResources();
            check(QTest::qWaitFor([&] { return !restarted.assetBusy() && !restarted.meshBusy(); }, 15000) &&
                      restarted.world().resourcePaths == QStringList{resource},
                  "fresh editor automatically reloads persistent default");
            const auto previousAssets = restarted.world().assets;
            QSignalSpy failures(&restarted, &Editor::failure);
            restarted.importResource(QUrl::fromLocalFile(tmp.filePath("missing.jar")), false);
            check(QTest::qWaitFor([&] { return !restarted.assetBusy(); }, 15000) && failures.size() == 1 &&
                      restarted.world().assets == previousAssets &&
                      restarted.defaultResourceSources() == QStringList{resource},
                  "failed manual import preserves current and default resources");
            QFile changedResource(resource);
            check(changedResource.open(QIODevice::Append) && changedResource.write("\n") == 1,
                  "modify resource cache fixture");
            changedResource.close();
            restarted.newWorld();
            check(QTest::qWaitFor([&] { return !restarted.assetBusy() && !restarted.meshBusy(); }, 15000) &&
                      restarted.world().assets && restarted.world().assets != previousAssets &&
                      !restarted.assetLog().contains("复用"),
                  "changed resource invalidates in-process cache");
        }
        check(editor.world().assets && editor.world().assets->thumbnails.contains("minecraft:block/stone"),
              "texture-only pack applied");
        check(!editor.blockCatalog("stone").isEmpty() &&
                  editor.blockCatalog("stone")[0].toMap()["name"] == "minecraft:stone",
              "exact catalog result ranks first");
        check(!editor.blockCatalog("shitou").isEmpty() && !editor.blockCatalog("st").isEmpty(),
              "pinyin and initials UI search");
        check(!editor.blockCatalog("石").isEmpty(), "Chinese material search");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("resources.wbench"))), "save resource paths");
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("resources.wbench"))), "reopen resource project");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 10000),
              "asynchronous resource restoration");
        check(editor.world().assets && !editor.modified(), "resource restoration retains saved state");
        const auto defaultBefore = editor.defaultResourceSources();
        const auto alternate = tmp.filePath("alternate.zip");
        QFile::copy(resource, alternate);
        auto explicitWorld = editor.world();
        explicitWorld.resourcePaths = {alternate};
        QString projectError;
        check(wb::saveProject(tmp.filePath("explicit.wbench"), explicitWorld, projectError),
              "project-specific resource fixture");
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("explicit.wbench"))),
              "open project-specific resources");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.world().resourcePaths == QStringList{alternate} &&
                  editor.defaultResourceSources() == defaultBefore,
              "project sources take precedence without changing defaults");
        editor.removeResource(0);
        check(!editor.world().assets && editor.modified(), "removing resource marks project modified");
        check(editor.defaultResourceSources().isEmpty(), "removing base clears remembered defaults");
        check(QTest::qWaitFor([&] { return !win->property("autoCloseResources").toBool(); }, 3000),
              "resource auto-close drains after rapid repeated loads");
        auto *manualSettings = win->findChild<QObject *>("editorSettingsDialog");
        win->setProperty("autoCloseResources", false);
        QMetaObject::invokeMethod(manualSettings, "open");
        QTest::qWait(500);
        check(manualSettings->property("visible").toBool(), "manually opened diagnostics stay open");
        QMetaObject::invokeMethod(manualSettings, "close");
        QElapsedTimer largeTimer;
        largeTimer.start();
        check(editor.createWorld(16384, 42, 24, 0, -60, true, 12289), "large rectangular creation");
        const auto creationMs = largeTimer.elapsed();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) &&
                  editor.scene()->chunks.size() <= 81 && !editor.scene()->farTiles.empty(),
              "large preview keeps bounded exact chunks and full far coverage");
        const auto firstLargePreview = largeTimer.elapsed();
        v->frameWorld();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "full map frame settles");
        QTest::qWait(150);
        win->grabWindow().save("test-output/full-16k-0.13.0.png");
        auto projectFar = [&](int x, int z) {
            auto p =
                v->viewProjection() * QVector4D(x + .5f, float(editor.world().height(x, z) + 1), z + .5f, 1);
            return v
                ->mapToScene(
                    QPointF((p.x() / p.w() * .5 + .5) * v->width(), (.5 - p.y() / p.w() * .5) * v->height()))
                .toPoint();
        };
        const auto distantScreen = projectFar(6000, 4000);
        const auto beforeJumpHeight = editor.world().height(6000, 4000);
        editor.setTool(-1);
        QTest::mouseDClick(win, Qt::LeftButton, Qt::NoModifier, distantScreen);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) && v->camera().y() < 500 &&
                  editor.previewContains(6000, 4000) && editor.world().height(6000, 4000) == beforeJumpHeight,
              "double clicking far terrain enters exact editing without sculpting proxy");
        editor.setTool(6);
        auto *selectAll = win->findChild<QObject *>("selectWholeMap");
        check(selectAll && QMetaObject::invokeMethod(selectAll, "clicked"), "select whole map action");
        check(editor.selectionRect() == QRect(editor.world().originX, editor.world().originZ, 16384, 12289),
              "whole-map selection covers rectangular world including edge columns");
        QTest::qWait(30);
        auto *applyHeight = win->findChild<QObject *>("applySelectionHeight");
        check(applyHeight && !applyHeight->property("enabled").toBool(),
              "oversized region action is visibly disabled instead of silently truncating");
        for (const auto &entry : {std::pair{"selectionMinX", 6000},
                                  {"selectionMinZ", 4000},
                                  {"selectionMaxX", 5969},
                                  {"selectionMaxZ", 3969}}) {
            auto *field = win->findChild<QObject *>(entry.first);
            check(field && field->setProperty("value", entry.second), "coordinate selection input");
        }
        auto *applyBounds = win->findChild<QObject *>("applySelectionBounds");
        check(applyBounds && QMetaObject::invokeMethod(applyBounds, "clicked"), "apply coordinate selection");
        check(editor.selectionRect() == QRect(5969, 3969, 32, 32),
              "coordinate selection normalizes reversed endpoints inclusively");
        auto *focusSelection = win->findChild<QObject *>("focusSelection");
        check(focusSelection && QMetaObject::invokeMethod(focusSelection, "clicked") &&
                  editor.previewContains(5984, 3984),
              "selection focus loads exact editing area at authoritative coordinates");
        editor.clearSelection();
        editor.setTool(-1);
        v->frameWorld();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "return to full map");
        QTest::qWait(550);
        const auto fullScene = editor.scene();
        check(!fullScene->farTiles.empty() &&
                  fullScene->bufferBytes + fullScene->colorBytes <= 512ull * 1024 * 1024,
              "bounded precision 16k flat overview geometry");
        editor.setTool(0);
        editor.setRadius(16);
        editor.setStrength(4);
        QTest::qWait(100); // Let the dock/tool panel layout settle before projecting.
        const auto remotePoint = projectFar(6000, 4000);
        const auto localPoint = v->mapFromScene(remotePoint);
        auto remoteDirection =
            v->viewProjection().inverted() * QVector4D(float(localPoint.x() / v->width() * 2 - 1),
                                                       float(1 - localPoint.y() / v->height() * 2), 1, 1);
        const auto remoteHit =
            editor.world().raycast(v->camera(), remoteDirection.toVector3DAffine() - v->camera(), 100000);
        check(remoteHit.valid && !editor.previewContains(remoteHit.x, remoteHit.z),
              "remote brush fixture starts outside exact residency");
        const auto remoteCamera = v->camera();
        const auto remoteMatrix = v->viewProjection();
        const auto remoteBefore = editor.world().heights;
        const int remoteHeight = editor.world().height(remoteHit.x, remoteHit.z);
        // This can be the same native pointer position as the earlier double-click.
        QTest::mouseMove(win, remotePoint + QPoint(5, 0), 20);
        QTest::mouseMove(win, remotePoint, 20);
        check(QTest::qWaitFor([&] { return v->hitText().contains("远处精确落点"); }, 2000),
              "surface brush reports exact remote target");
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, remotePoint);
        QTest::qWait(230);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, remotePoint);
        if (editor.world().height(remoteHit.x, remoteHit.z) <= remoteHeight || v->camera() != remoteCamera ||
            v->viewProjection() != remoteMatrix) {
            std::cerr << "Remote brush expected " << remoteHit.x << "," << remoteHit.z << " actual "
                      << v->hit().x << "," << v->hit().z << " before/after " << remoteHeight << "/"
                      << editor.world().height(remoteHit.x, remoteHit.z) << " cameraChanged "
                      << (v->camera() != remoteCamera) << " projectionChanged "
                      << (v->viewProjection() != remoteMatrix) << "\n";
        }
        check(editor.world().height(remoteHit.x, remoteHit.z) > remoteHeight && v->camera() == remoteCamera &&
                  v->viewProjection() == remoteMatrix,
              "stationary remote brush edits authoritative column without moving camera");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) &&
                  !editor.previewContains(remoteHit.x, remoteHit.z) &&
                  editor.scene()->exactArea == fullScene->exactArea,
              "remote brush keeps continuous terrain and does not move exact residency");
        win->grabWindow().save("test-output/remote-brush-0.13.0.png");
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        check(editor.world().heights == remoteBefore, "remote held stroke is one keyboard undo");
        QTest::keyClick(win, Qt::Key_Y, Qt::ControlModifier);
        check(editor.world().height(remoteHit.x, remoteHit.z) > remoteHeight, "remote stroke redo");
        editor.undo();
        // Unloaded model geometry must still gate volume and material tools.
        v->frameWorld();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "reset exact residency");
        for (int tool : {2, 3, 4}) {
            editor.setTool(tool);
            const auto rev = editor.revision();
            QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, projectFar(6000, 4000));
            check(editor.revision() == rev && !editor.commandBusy(),
                  "remote volume and material clicks do not write or leave active strokes");
        }
        editor.setTool(6);
        editor.setSelection(5000, 3000, 6023, 4023);
        const auto cancelRevision = editor.revision();
        const auto cancelLayer = editor.selectedNode();
        const auto cancelHeights = editor.world().heights;
        check(editor.adjustSelectedHeight(6) && editor.regionBusy(), "large asynchronous region starts");
        editor.setSelectedNode("environment-light");
        check(editor.selectedNode() == cancelLayer && !editor.adjustSelectedHeight(2),
              "region transaction prevents competing layer selection and operation");
        QTest::qWait(20);
        auto *cancelButton = win->findChild<QObject *>("cancelRegionButton");
        auto *progressBar = win->findChild<QObject *>("regionProgressBar");
        check(cancelButton && cancelButton->property("visible").toBool() && progressBar &&
                  progressBar->property("visible").toBool() && editor.regionProgress() >= 0 &&
                  editor.regionProgress() <= 99,
              "progress and cancel controls appear while region is running");
        win->grabWindow().save("test-output/region-progress-0.13.0.png");
        check(QMetaObject::invokeMethod(cancelButton, "clicked"), "cancel from actual UI control");
        check(QTest::qWaitFor([&] { return !editor.regionBusy(); }, 10000) &&
                  editor.revision() == cancelRevision && editor.world().heights == cancelHeights &&
                  editor.status().contains("已取消"),
              "cancel discards staged edits without revision or history changes");
        editor.setSelection(5000, 3000, 5512, 3512);
        check(editor.adjustSelectedHeight(6), "region over old 512-square limit accepted");
        check(QTest::qWaitFor([&] { return !editor.regionBusy() && !editor.meshBusy(); }, 30000) &&
                  editor.world().height(5000, 3000) == cancelHeights[editor.world().column(5000, 3000)] + 6 &&
                  editor.regionProgress() == 100 && editor.revision() == cancelRevision + 1,
              "large region publishes once and refreshes visible far tiles");
        editor.undo();
        check(editor.world().heights == cancelHeights, "large region undo restores exact layer snapshot");
        editor.redo();
        check(editor.world().height(5512, 3512) == cancelHeights[editor.world().column(5512, 3512)] + 6,
              "large region redo restores far corner");
        editor.undo();
        editor.clearSelection();
        editor.setTool(-1);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "large history settles");
        // Edit away from the resident exact area, then verify the far summary is refreshed.
        editor.executeTool("set_blocks",
                           {{"blocks", QJsonArray{QJsonObject{{"position", QJsonArray{7000, 300, 5000}},
                                                              {"block", "minecraft:oak_planks"}}}},
                            {"expected_revision", double(editor.revision())}});
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000),
              "distant MCP edit refreshes far tiles");
        auto distantTop = [&] {
            float top = -1000;
            for (const auto &t : editor.scene()->farTiles)
                if (t.area.contains(7000, 5000))
                    top = t.mesh->upper.y();
            return top;
        };
        check(distantTop() < 301 && editor.world().block(7000, 300, 5000) == wb::Planks,
              "distant heightfield does not distort terrain into an artificial structure envelope");
        v->jumpTo(7000, 5000);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) &&
                  editor.previewContains(7000, 5000),
              "structure is available in automatic exact inspection");
        bool structureVisible = false;
        for (const auto &mesh : editor.scene()->chunks)
            for (const auto &vertex : mesh->opaque)
                structureVisible |= vertex.x >= 7000 && vertex.x <= 7001 && vertex.z >= 5000 &&
                                    vertex.z <= 5001 && vertex.y == 301;
        check(structureVisible,
              "local exact mesh includes the real structure without spreading its footprint");
        editor.undo();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) && distantTop() < 301,
              "undo invalidates distant summary");
        v->jumpTo(8000, 6000);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) &&
                  editor.previewContains(8000, 6000),
              "large map jump loads distant edit region");
        editor.setSelection(7998, 5998, 8002, 6002);
        editor.setSelectionOnly(true);
        editor.setTool(0);
        editor.setRadius(4);
        const int beforeInside = editor.world().height(8000, 6000),
                  beforeOutside = editor.world().height(8003, 6000);
        editor.beginStroke();
        editor.stamp({true,
                      8000,
                      beforeInside,
                      6000,
                      {8000.5f, float(beforeInside + 1), 6000.5f},
                      {0, 1, 0},
                      wb::Grass},
                     false);
        editor.endStroke();
        check(editor.world().height(8000, 6000) > beforeInside &&
                  editor.world().height(8003, 6000) == beforeOutside,
              "GUI brush respects active selection at distant coordinates");
        QSignalSpy selectedExport(&editor, &Editor::exported);
        editor.exportWorld(QUrl::fromLocalFile(tmp.path()), "local-test", true);
        check(QTest::qWaitFor([&] { return !editor.exportBusy(); }, 30000) && selectedExport.size() == 1 &&
                  QFile::exists(tmp.filePath("local-test/level.dat")),
              "editor local export completes without baking full 16K world");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("large.wbench"))) &&
                  editor.open(QUrl::fromLocalFile(tmp.filePath("large.wbench"))) &&
                  editor.world().width == 16384 && editor.world().depth == 12289 &&
                  editor.world().height(8000, 6000) > beforeInside,
              "large edited project save and reopen");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "reopened large preview settles");
        v->frameWorld();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000), "reopened whole map frames");
        check(!editor.selectionRect().isValid() && !editor.selectionOnly(), "open clears session selection");
        std::cout << "Large 16384x12289 creation " << creationMs << " ms / first local preview "
                  << firstLargePreview << " ms / " << editor.scene()->chunks.size() << " chunks\n";
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/large-0.13.0.png"), "capture large local preview");
        editor.createWorld(128, 42, 24, 0, -60, true);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "theme fixture ready");
        auto *themeDialog = win->findChild<QObject *>("surfaceThemeDialog");
        check(themeDialog && QMetaObject::invokeMethod(themeDialog, "open"), "theme editor opens");
        QTest::qWait(80);
        auto *themeEnabled = visualItem(win->contentItem(), "themeEnabled");
        auto *themeApply = visualItem(win->contentItem(), "applySurfaceTheme");
        check(themeEnabled && themeApply, "theme controls available");
        auto *defaultBand = visualItem(win->contentItem(), "themeDefaultBand");
        auto *noiseInput = visualItem(win->contentItem(), "themeNoise");
        check(defaultBand && defaultBand->isVisible() && noiseInput && !noiseInput->isEnabled(),
              "default band is a label and disabled theme cannot edit ineffective noise");
        QTest::mouseClick(
            win, Qt::LeftButton, Qt::NoModifier,
            themeEnabled->mapToScene({themeEnabled->width() / 2, themeEnabled->height() / 2}).toPoint());
        check(QMetaObject::invokeMethod(themeDialog, "chooseMaterial", Q_ARG(QVariant, 0),
                                        Q_ARG(QVariant, QString("surface"))),
              "theme material picker opens");
        QTest::qWait(80);
        auto *planksChoice = visualItem(win->contentItem(), "themeMaterialRow_minecraft:oak_planks");
        check(planksChoice, "theme material catalog includes native planks");
        QTest::mouseClick(
            win, Qt::LeftButton, Qt::NoModifier,
            planksChoice->mapToScene({planksChoice->width() / 2, planksChoice->height() / 2}).toPoint());
        QTest::mouseClick(
            win, Qt::LeftButton, Qt::NoModifier,
            themeApply->mapToScene({themeApply->width() / 2, themeApply->height() / 2}).toPoint());
        check(editor.world().theme.enabled && editor.world().block(0, 24, 0) == wb::Planks,
              "UI theme applies chosen surface block");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "theme preview settles");
        win->grabWindow().save("test-output/theme-controls-0.13.0.png");
        check(QMetaObject::invokeMethod(themeDialog, "close"), "close theme editor");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("theme-ui.wbench"))), "save GUI theme");
        editor.undo();
        check(!editor.world().theme.enabled && editor.world().block(0, 24, 0) == wb::Grass,
              "theme is one undo");
        editor.redo();
        check(editor.world().theme.enabled && editor.world().block(0, 24, 0) == wb::Planks,
              "theme redo restores live result");
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("theme-ui.wbench"))) &&
                  editor.world().theme.enabled,
              "GUI theme project reopens");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000),
              "theme reopen settles");
        check(QMetaObject::invokeMethod(themeDialog, "open"), "reopen theme for noise regression");
        QTest::qWait(80);
        auto *boundaryInput = visualItem(win->contentItem(), "themeHeight1");
        check(boundaryInput && boundaryInput->isEnabled(), "nondefault boundary remains editable");
        boundaryInput->setProperty("value", 24);
        check(QMetaObject::invokeMethod(boundaryInput, "valueModified"), "edit actual height boundary");
        check(QMetaObject::invokeMethod(themeDialog, "apply"), "apply boundary without noise");
        check(editor.world().block(0, 24, 0) == wb::Stone, "zero noise gives high band at threshold");
        noiseInput->setProperty("value", 8);
        check(QMetaObject::invokeMethod(themeDialog, "apply"), "apply noise through theme UI");
        int noisyPlanks = 0, noisyStone = 0;
        for (int z = -64; z < 64; ++z)
            for (int x = -64; x < 64; ++x) {
                noisyPlanks += editor.world().block(x, 24, z) == wb::Planks;
                noisyStone += editor.world().block(x, 24, z) == wb::Stone;
            }
        check(noisyPlanks > 100 && noisyStone > 100 && editor.world().height(0, 0) == 24,
              "UI noise visibly changes material distribution without changing geometry");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "noise preview settles");
        win->grabWindow().save("test-output/theme-noise-0.13.0.png");
        check(QMetaObject::invokeMethod(themeDialog, "close"), "close noise regression dialog");
        QTest::qWait(100);
        win->grabWindow().save("test-output/theme-noise-surface-0.13.0.png");
        editor.createWorld(128, 42, 24, 0, -60, true);
        if (app.arguments().size() > 1) {
            check(editor.open(QUrl::fromLocalFile(app.arguments()[1])), "open real resource preview fixture");
            check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 120000),
                  "real resource preview finishes");
            check(editor.world().assets && editor.world().assets->skippedEntries == 25,
                  "real pack import diagnostics reach editor");
            check(!editor.blockCatalog("铁砧").isEmpty(), "real JAR Chinese search reaches UI catalog");
            const auto gold = editor.resolveThemeMaterial("minecraft:gold_block");
            check(gold >= wb::BlockCount, "theme picker resolves real resource material");
            auto theme = editor.surfaceTheme();
            auto bands = theme["bands"].toList();
            auto band = bands.front().toMap();
            band["surface"] = gold;
            bands[0] = band;
            theme["bands"] = bands;
            theme["enabled"] = true;
            theme["beaches"] = false;
            check(editor.applySurfaceTheme(theme), "real resource theme applies");
            check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000) &&
                      editor.world().block(0, editor.world().height(0, 0), 0) == wb::Block(gold),
                  "real theme material appears on generated surface");
            editor.undo();
        }
        QMetaObject::invokeMethod(win->findChild<QObject *>("editorSettingsDialog"), "close");
        editor.setTool(4);
        v->resetCamera();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "final layout mesh settles");
        QTest::qWait(120);
        QDir().mkpath("test-output");
        check(win->grabWindow().save("test-output/ui-0.13.0.png"), "capture brush and scene layout");
        auto *settings = win->findChild<QObject *>("editorSettingsDialog");
        auto *settingsContent = win->findChild<QObject *>("settingsContent");
        check(settings && settingsContent, "settings pages loaded outside scene outliner");
        settingsContent->setProperty("page", "resources");
        check(QMetaObject::invokeMethod(settings, "open"), "resource settings opens");
        QTest::qWait(80);
        check(win->grabWindow().save("test-output/resources-0.13.0.png"), "capture resource management");
        QMetaObject::invokeMethod(settings, "close");
        auto *fileMenu = win->findChild<QObject *>("fileMenu");
        check(fileMenu && QMetaObject::invokeMethod(fileMenu, "open"), "custom file menu opens");
        fileMenu->setProperty("currentIndex", 1);
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/menu-hover-0.13.0.png"), "capture menu highlight contrast");
        QMetaObject::invokeMethod(fileMenu, "close");
        auto *newWorldDialog = win->findChild<QObject *>("newWorldDialog");
        check(newWorldDialog && QMetaObject::invokeMethod(newWorldDialog, "open"), "creation dialog opens");
        QTest::qWait(100);
        auto *widthControl = win->findChild<QObject *>("newMapWidth");
        widthControl->setProperty("value", 16384);
        auto *numberInput =
            qobject_cast<QQuickItem *>(widthControl->property("contentItem").value<QObject *>());
        check(numberInput && numberInput->property("text").toString() == "16384",
              "numeric editor omits grouping separators");
        const auto numericPoint =
            numberInput->mapToScene({numberInput->width() / 2, numberInput->height() / 2}).toPoint();
        auto *numberWindow = numberInput->window();
        check(numberWindow, "numeric field has a window");
        numberWindow->requestActivate();
        QTest::qWait(80);
        QTest::mouseClick(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::qWait(50);
        check(numberInput->property("selectedText").toString() == "16384",
              "numeric click selects the complete value");
        for (const auto key : {Qt::Key_4, Qt::Key_0, Qt::Key_9, Qt::Key_6})
            QTest::keyClick(numberWindow, key);
        QTest::keyClick(numberWindow, Qt::Key_Return);
        check(widthControl->property("value").toInt() == 4096,
              "selected number replaces and commits correctly");
        QTest::mouseClick(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::qWait(50);
        check(numberInput->property("selectedText").toString().isEmpty(),
              "subsequent single click places caret");
        QTest::mouseDClick(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::qWait(30);
        check(numberInput->property("selectedText").toString() == "4096",
              "double click selects entire numeric value");
        for (auto key : {Qt::Key_2, Qt::Key_5, Qt::Key_6, Qt::Key_Slash, Qt::Key_2})
            QTest::keyClick(numberWindow, key);
        QTest::keyClick(numberWindow, Qt::Key_Return);
        check(widthControl->property("value").toInt() == 128,
              "typed arithmetic expression calculates on commit");
        // Regression: the old implementation only allowed a 14px left grip
        // after text focus. Test the middle of the field, exactly after typing.
        QTest::mousePress(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::mouseMove(numberWindow, numericPoint + QPoint(30, 0), 40);
        QTest::mouseRelease(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint + QPoint(30, 0));
        check(widthControl->property("value").toInt() == 138,
              "center drag still scrubs after typing and committing expression");
        QTest::mousePress(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::mouseMove(numberWindow, numericPoint - QPoint(15, 0), 40);
        check(widthControl->property("value").toInt() == 133, "repeat drag moves number left");
        QTest::keyClick(numberWindow, Qt::Key_Escape);
        QTest::mouseRelease(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint - QPoint(15, 0));
        check(widthControl->property("value").toInt() == 138, "Escape cancels focused center drag");
        QTest::mouseClick(numberWindow, Qt::LeftButton, Qt::NoModifier, numericPoint);
        QTest::keyClick(numberWindow, Qt::Key_A, Qt::ControlModifier);
        for (auto key : {Qt::Key_1, Qt::Key_2, Qt::Key_8})
            QTest::keyClick(numberWindow, key);
        QTest::keyClick(numberWindow, Qt::Key_Return);
        QTest::keyClick(numberWindow, Qt::Key_A, Qt::ControlModifier);
        for (auto key : {Qt::Key_2, Qt::Key_Slash, Qt::Key_0})
            QTest::keyClick(numberWindow, key);
        QTest::keyClick(numberWindow, Qt::Key_Return);
        check(widthControl->property("value").toInt() == 128, "division by zero leaves value unchanged");
        auto scrubStart = numberInput->mapToScene({7, numberInput->height() / 2}).toPoint();
        QTest::mousePress(numberWindow, Qt::LeftButton, Qt::NoModifier, scrubStart);
        QTest::mouseMove(numberWindow, scrubStart + QPoint(60, 0), 40);
        QTest::mouseRelease(numberWindow, Qt::LeftButton, Qt::NoModifier, scrubStart + QPoint(60, 0));
        check(widthControl->property("value").toInt() > 128,
              "numeric drag scrubs value while already focused");
        check(win->grabWindow().save("test-output/new-world-0.13.0.png"), "capture creation settings");
        QMetaObject::invokeMethod(newWorldDialog, "close");
        editor.setTool(1);
        editor.setBrushOption("fillHoles", true);
        editor.setBrushOption("filterEnabled", true);
        auto *selector = win->findChild<QObject *>("flattenModeSelector");
        auto *popup = selector ? selector->property("popup").value<QObject *>() : nullptr;
        check(popup && QMetaObject::invokeMethod(popup, "open"), "flatten options dropdown opens");
        selector->setProperty("highlightedIndex", 1);
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/flatten-controls-0.13.0.png"), "capture flatten controls");
        QMetaObject::invokeMethod(popup, "close");
        // A long real outliner, scrolled to an actual eye button. Visibility,
        // undo/redo and asynchronous scene notifications must retain that row.
        check(editor.createWorld(32, 42, 24, 0, -60, true), "outliner fixture");
        for (int i = 0; i < 22; ++i)
            editor.addSculptLayer();
        QTest::qWait(100);
        auto *list = win->findChild<QQuickItem *>("sceneNodeList");
        check(list && list->property("count").toInt() == 28, "stable model contains all scene nodes");
        list->setProperty("contentY", 360);
        QTest::qWait(80);
        const auto nodeId = editor.sceneNodes()[12].toMap()["id"].toString();
        editor.setSelectedNode(nodeId);
        auto *scrolledEye = visualItem(list, "eye_" + nodeId);
        check(scrolledEye && scrolledEye->isVisible(), "scrolled eye delegate exists");
        const qreal scrollBefore = list->property("contentY").toReal();
        const auto eyePoint =
            scrolledEye->mapToScene({scrolledEye->width() / 2, scrolledEye->height() / 2}).toPoint();
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, eyePoint);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 10000), "visibility refresh settles");
        check(!editor.layerDetails()["enabled"].toBool() && editor.selectedNode() == nodeId &&
                  std::abs(list->property("contentY").toReal() - scrollBefore) < 1,
              "eye toggles actual layer without resetting outliner scroll or selection");
        editor.undo();
        editor.redo();
        editor.setEnvironment({{"haze", 80}});
        QTest::qWait(100);
        check(std::abs(list->property("contentY").toReal() - scrollBefore) < 1,
              "history and unrelated notifications retain outliner scroll");
        check(win->grabWindow().save("test-output/outliner-stable-0.13.0.png"), "capture scrolled outliner");
        std::cout << "PASS " << checks << " UI integration checks; mesh settle " << elapsed.elapsed()
                  << " ms\n";
        win->hide();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
