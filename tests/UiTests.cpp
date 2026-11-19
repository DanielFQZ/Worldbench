#include "Editor.h"
#include "Viewport.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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
        check(tools["result"].toObject()["tools"].toArray().size() == 10, "MCP discoverable tools");
        auto call = [&](QString name, QJsonObject args) {
            return request("tools/call", {{"name", name}, {"arguments", args}})["result"].toObject();
        };
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
        result = call("catalog", {{"query", "stone"}});
        check(!result["isError"].toBool(), "MCP block catalog");
        result = call("camera", {{"position", QJsonArray{10, 40, 60}}, {"yaw", -10}, {"pitch", -15}});
        check(!result["isError"].toBool() && v->camera() == QVector3D(10, 40, 60), "MCP camera control");
        check(QTest::qWaitFor([&] { return !editor.meshBusy(); }, 10000), "MCP mesh drains");
        result = call("screenshot", {});
        check(!result["isError"].toBool() && result["content"].toArray()[0].toObject()["type"] == "image",
              "MCP image result");
        editor.setMcpEnabled(false);
        check(!editor.mcpEnabled(), "MCP disabled");
        bridge.closeWriteChannel();
        check(QTest::qWaitFor([&] { return bridge.state() == QProcess::NotRunning; }, 5000),
              "MCP bridge exits on stdin EOF");
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
        editor.importResource(QUrl::fromLocalFile(resource), false);
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 10000),
              "async resource import settles");
        check(editor.world().assets && editor.world().assets->thumbnails.contains("minecraft:block/stone"),
              "texture-only pack applied");
        check(editor.blockCatalog("stone").size() == 1, "searchable material catalog");
        check(editor.save(QUrl::fromLocalFile(tmp.filePath("resources.wbench"))), "save resource paths");
        check(editor.open(QUrl::fromLocalFile(tmp.filePath("resources.wbench"))), "reopen resource project");
        check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 10000),
              "asynchronous resource restoration");
        check(editor.world().assets && !editor.modified(), "resource restoration retains saved state");
        editor.removeResource(0);
        check(!editor.world().assets && editor.modified(), "removing resource marks project modified");
        std::cout << "PASS " << checks << " UI integration checks; mesh settle " << elapsed.elapsed()
                  << " ms\n";
        win->hide();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
