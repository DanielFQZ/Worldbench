#include "Editor.h"
#include "Viewport.h"
#include "core/Project.h"
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
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
        check(!engine.rootObjects().isEmpty(), "QML window loads");
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        check(win, "native Qt window");
        win->requestActivate();
        check(QTest::qWaitForWindowExposed(win), "window exposed");
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
        check(editor.sceneNodes().size() == 1 && editor.sceneNodes()[0].toMap()["name"] == "基础地形层1",
              "outliner contains only actual terrain layer");
        editor.setTool(-1);
        QTest::qWait(40);
        check(!brushPanel->isVisible(), "navigation collapses brush settings");
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
        check(tools["result"].toObject()["tools"].toArray().size() == 11, "MCP discoverable tools");
        auto call = [&](QString name, QJsonObject args) {
            return request("tools/call", {{"name", name}, {"arguments", args}})["result"].toObject();
        };
        auto sceneResult = call("world_info", {});
        auto sceneInfo = QJsonDocument::fromJson(
                             sceneResult["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                             .object();
        check(sceneInfo["scene_nodes"].toArray().size() == 1 &&
                  sceneInfo["scene_nodes"].toArray()[0].toObject()["id"] == "terrain-base-1",
              "MCP uses same actual scene nodes as outliner");
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
        check(houseImage.save("test-output/mcp-house-0.5.0.png"), "capture MCP house");
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
        editor.importHeightmap(QUrl::fromLocalFile(tmp.filePath("hill.png")));
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 15000) &&
                  editor.sceneNodes().size() == 2 && editor.world().height(0, 0) > beforeImport,
              "heightmap imported as a composed scene component");
        auto instanceId = editor.selectedNode();
        auto *instancePanel = win->findChild<QQuickItem *>("terrainInstanceControls");
        check(instancePanel && instancePanel->isVisible(), "selected instance exposes detail controls");
        auto *terrainX = win->findChild<QQuickItem *>("terrainX");
        check(QTest::qWaitFor(
                  [&] {
                      return terrainX && terrainX->isVisible() && terrainX->width() > 40 &&
                             terrainX->height() > 20 && instancePanel->height() > 400;
                  },
                  3000),
              "terrain details lay out actual numeric controls");
        auto *terrainInput = qobject_cast<QQuickItem *>(terrainX->property("contentItem").value<QObject *>());
        check(terrainInput && terrainInput->property("text").toString() == "-32",
              "terrain coordinate control reflects imported origin");
        auto *terrainWindow = terrainInput->window();
        terrainWindow->requestActivate();
        QTest::qWait(80);
        QTest::mouseClick(
            terrainWindow, Qt::LeftButton, Qt::NoModifier,
            terrainInput->mapToScene({terrainInput->width() / 2, terrainInput->height() / 2}).toPoint());
        QTest::qWait(50);
        check(terrainInput->property("selectedText").toString() == "-32",
              "terrain number click selects value");
        QTest::keyClick(terrainWindow, Qt::Key_1);
        QTest::keyClick(terrainWindow, Qt::Key_6);
        QTest::keyClick(terrainWindow, Qt::Key_Return);
        check(editor.world().heights.instances[0].x == 16, "terrain panel commits typed origin");
        editor.undo();
        check(editor.world().heights.instances[0].x == -32, "terrain panel input shares undo history");
        auto instanceScene = call("world_info", {});
        auto instanceInfo = QJsonDocument::fromJson(
                                instanceScene["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                                .object();
        check(instanceInfo["terrain_instances"].toArray().size() == 1 &&
                  instanceInfo["scene_nodes"].toArray().size() == 2,
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
                  editor.sceneNodes().size() == 3,
              "multiple instances in outliner");
        const auto topId = editor.selectedNode();
        editor.moveTerrain(-1);
        check(editor.world().heights.instances[0].id == topId, "instance ordering updates");
        editor.undo();
        check(editor.world().heights.instances[1].id == topId, "instance ordering undo");
        editor.removeTerrain();
        check(editor.sceneNodes().size() == 2, "remove instance");
        editor.undo();
        check(editor.sceneNodes().size() == 3, "undo removal restores instance");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("terrain.wbench"))), "save instance project");
        QFile::remove(tmp.filePath("hill.png"));
        QFile::remove(tmp.filePath("mask.png"));
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("terrain.wbench"))) &&
                  editor.sceneNodes().size() == 3,
              "embedded terrain reopens without PNGs");
        editor.setSelectedNode(topId);
        editor.setTool(-1);
        editor.focusTerrain();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "instance preview rendered");
        QTest::qWait(150);
        check(win->grabWindow().save("test-output/terrain-0.5.0.png"), "capture terrain instance controls");
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
        check(win->grabWindow().save("test-output/terrain-actions-0.5.0.png"),
              "capture lower terrain controls");
        detailFlickable->setProperty("contentY", 0);
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
        v->forceActiveFocus();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "stationary brush fixture ready");
        point = v->mapToScene(QPointF(v->width() * .5, v->height() * .6)).toPoint();
        QTest::mouseMove(win, point);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, point);
        const auto anchored = v->hit();
        QTest::qWait(550);
        const auto held = v->hit();
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, point);
        check(anchored.valid && held.x == anchored.x && held.z == anchored.z && held.y > anchored.y,
              "stationary held brush raises fixed XZ instead of repicking slope");
        editor.undo();
        editor.setRadius(12);
        editor.setBrushOption("decay", .5);
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "brush preview ready");
        QTest::mouseMove(win, point + QPoint(1, 0));
        QTest::qWait(120);
        check(win->grabWindow().save("test-output/brush-decay-0.5.0.png"), "capture both decay rings");
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
        check(editor.selectionRect().width() > 1 && editor.selectionRect().height() > 1 &&
                  editor.world().heights == beforeSelection && editor.revision() == selectionRevision,
              "actual mouse drag selects rectangle without changing terrain");
        QTest::qWait(120);
        check(win->grabWindow().save("test-output/selection-0.5.0.png"), "capture selection boundary");
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
        check(editor.createWorld(16384, 42, 24, 12, -60, false, 12289), "large rectangular creation");
        const auto creationMs = largeTimer.elapsed();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 20000) &&
                  editor.scene()->chunks.size() <= 81,
              "large preview stays within 81 exact chunks");
        const auto firstLargePreview = largeTimer.elapsed();
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
        check(!editor.selectionRect().isValid() && !editor.selectionOnly(), "open clears session selection");
        std::cout << "Large 16384x12289 creation " << creationMs << " ms / first local preview "
                  << firstLargePreview << " ms / " << editor.scene()->chunks.size() << " chunks\n";
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/large-0.5.0.png"), "capture large local preview");
        editor.createWorld(128, 42, 24, 0, -60, true);
        if (app.arguments().size() > 1) {
            check(editor.open(QUrl::fromLocalFile(app.arguments()[1])), "open real resource preview fixture");
            check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 120000),
                  "real resource preview finishes");
            check(editor.world().assets && editor.world().assets->skippedEntries == 25,
                  "real pack import diagnostics reach editor");
            check(!editor.blockCatalog("铁砧").isEmpty(), "real JAR Chinese search reaches UI catalog");
        }
        QMetaObject::invokeMethod(win->findChild<QObject *>("editorSettingsDialog"), "close");
        editor.setTool(4);
        v->resetCamera();
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 15000), "final layout mesh settles");
        QTest::qWait(120);
        QDir().mkpath("test-output");
        check(win->grabWindow().save("test-output/ui-0.5.0.png"), "capture brush and scene layout");
        auto *settings = win->findChild<QObject *>("editorSettingsDialog");
        auto *settingsContent = win->findChild<QObject *>("settingsContent");
        check(settings && settingsContent, "settings pages loaded outside scene outliner");
        settingsContent->setProperty("page", "resources");
        check(QMetaObject::invokeMethod(settings, "open"), "resource settings opens");
        QTest::qWait(80);
        check(win->grabWindow().save("test-output/resources-0.5.0.png"), "capture resource management");
        QMetaObject::invokeMethod(settings, "close");
        auto *fileMenu = win->findChild<QObject *>("fileMenu");
        check(fileMenu && QMetaObject::invokeMethod(fileMenu, "open"), "custom file menu opens");
        fileMenu->setProperty("currentIndex", 1);
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/menu-hover-0.5.0.png"), "capture menu highlight contrast");
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
        check(win->grabWindow().save("test-output/new-world-0.5.0.png"), "capture creation settings");
        QMetaObject::invokeMethod(newWorldDialog, "close");
        editor.setTool(1);
        editor.setBrushOption("fillHoles", true);
        editor.setBrushOption("filterEnabled", true);
        auto *selector = win->findChild<QObject *>("flattenModeSelector");
        auto *popup = selector ? selector->property("popup").value<QObject *>() : nullptr;
        check(popup && QMetaObject::invokeMethod(popup, "open"), "flatten options dropdown opens");
        selector->setProperty("highlightedIndex", 1);
        QTest::qWait(100);
        check(win->grabWindow().save("test-output/flatten-controls-0.5.0.png"), "capture flatten controls");
        QMetaObject::invokeMethod(popup, "close");
        std::cout << "PASS " << checks << " UI integration checks; mesh settle " << elapsed.elapsed()
                  << " ms\n";
        win->hide();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
