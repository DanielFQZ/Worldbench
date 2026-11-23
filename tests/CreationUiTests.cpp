#include "Editor.h"
#include "Viewport.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray digest(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "source file readable");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result();
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Worldbench creation integration tests");
    QTemporaryDir settings, temporaryOutput;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle("Basic");
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    qmlRegisterType<Viewport>("Worldbench", 1, 0, "VoxelViewport");
    qmlRegisterUncreatableType<Editor>("Worldbench", 1, 0, "WorldEditor", "Application instance only");
    try {
        const auto args = app.arguments();
        const QString out = args.value(1, temporaryOutput.path()),
                      source = args.value(2).startsWith("--") ? QString{} : args.value(2);
        const QString skyFile = args.contains("--sky") ? args.value(args.indexOf("--sky") + 1) : QString{};
        if (args.size() > 1)
            check(!QFile::exists(out), "output directory must be new");
        check(QDir().mkpath(out), "create output directory");
        const auto sourceHash = source.isEmpty() ? QByteArray{} : digest(source);
        Editor editor;
        editor.setTool(-1);
        QQmlApplicationEngine engine;
        KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.load(QUrl("qrc:/main.qml"));
        check(!engine.rootObjects().isEmpty(), "real editor UI loads");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        check(window && QTest::qWaitForWindowExposed(window), "D3D11 window exposed");
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "creation test window active for keyboard shortcuts");
        auto *viewport = window->findChild<Viewport *>("voxelViewport");
        check(viewport, "actual viewport available");
        viewport->setRawInput(false);
        QString lastFailure;
        QObject::connect(&editor, &Editor::failure, &app, [&](QString message) {
            lastFailure = message;
            std::cerr << message.toStdString() << "\n";
        });
        auto settle = [&] {
            check(QTest::qWaitFor([&] { return !editor.meshBusy() && !editor.assetBusy(); }, 300000),
                  "background scene settles");
            check(lastFailure.isEmpty(), "editor reports no failure");
            const int frames = viewport->frameCounter()->load();
            viewport->update();
            check(QTest::qWaitFor([&] { return viewport->frameCounter()->load() > frames; }, 15000),
                  "new scene rendered on GPU");
            QTest::qWait(100);
        };
        settle();
        check(editor.createWorld(128, 731, 24, 0, 8, true, 128), "create creation fixture");
        settle();
        const QString jar = "E:/AppProject/.worldbench-tools/minecraft-1.20.1-client.jar";
        if (QFile::exists(jar)) {
            editor.importResource(QUrl::fromLocalFile(jar), false);
            settle();
        }
        editor.setMcpEnabled(true);
        const auto config = QJsonDocument::fromJson(editor.mcpConfiguration().toUtf8())
                                .object()["mcpServers"]
                                .toObject()["worldbench"]
                                .toObject();
        QProcess bridge;
        QStringList bridgeArgs;
        for (auto a : config["args"].toArray())
            bridgeArgs << a.toString();
        bridge.start(config["command"].toString(), bridgeArgs);
        check(bridge.waitForStarted(5000), "real MCP stdio process starts");
        int id = 0;
        QByteArray incoming;
        QJsonArray transcript;
        auto request = [&](QString method, QJsonObject params) {
            const int requestId = ++id;
            bridge.write(QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                                   {"id", requestId},
                                                   {"method", method},
                                                   {"params", params}})
                             .toJson(QJsonDocument::Compact) +
                         '\n');
            check(QTest::qWaitFor(
                      [&] {
                          incoming += bridge.readAllStandardOutput();
                          return incoming.contains('\n');
                      },
                      120000),
                  "MCP response arrives");
            const int end = incoming.indexOf('\n');
            auto response = QJsonDocument::fromJson(incoming.left(end)).object();
            incoming.remove(0, end + 1);
            check(response["id"].toInt() == requestId && !response.contains("error"),
                  "MCP response id and protocol");
            return response["result"].toObject();
        };
        auto init = request(
            "initialize",
            {{"protocolVersion", "2025-06-18"},
             {"capabilities", QJsonObject{}},
             {"clientInfo", QJsonObject{{"name", "Worldbench terrain verification"}, {"version", "1"}}}});
        check(init["protocolVersion"] == "2025-06-18", "MCP negotiated");
        bridge.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        check(request("tools/list", {})["tools"].toArray().size() == 16, "MCP discovers current tool schema");
        const auto initialCamera = viewport->camera();
        auto rejected =
            request("tools/call", {{"name", "camera"},
                                   {"arguments", QJsonObject{{"surface_preview", true}, {"pitch", 1000}}}});
        check(rejected["isError"].toBool() && viewport->camera() == initialCamera,
              "invalid combined camera request leaves camera unchanged");
        rejected = request("tools/call",
                           {{"name", "camera"}, {"arguments", QJsonObject{{"surface_preview", false}}}});
        check(rejected["isError"].toBool() && viewport->camera() == initialCamera,
              "retired prototype parameter cannot select a legacy renderer");
        auto call = [&](QString name, QJsonObject arguments) {
            auto result = request("tools/call", {{"name", name}, {"arguments", arguments}});
            if (result["isError"].toBool())
                std::cerr << QJsonDocument(result).toJson().constData();
            check(!result["isError"].toBool(), "MCP tool succeeds");
            if (name != "screenshot")
                transcript.append(QJsonObject{{"tool", name}, {"arguments", arguments}, {"result", result}});
            return result;
        };
        auto info = [&] {
            return QJsonDocument::fromJson(
                       call("world_info", {})["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                .object();
        };
        auto write = [&](QString name, QJsonObject arguments) {
            arguments["expected_revision"] = info()["revision"];
            return call(name, arguments);
        };

        auto jsonFile = [&](QString name, QJsonObject object) {
            QFile f(out + "/" + name);
            check(f.open(QIODevice::WriteOnly), "fixture writable");
            f.write(QJsonDocument(object).toJson());
            return f.fileName();
        };
        auto add = [&](QString kind, QJsonObject changes) {
            return write("creation", {{"action", "edit"},
                                      {"operation", "add"},
                                      {"data", QJsonObject{{"kind", kind}, {"changes", changes}}}});
        };
        auto patch = [&](QString kind, QString objectId, QJsonObject changes) {
            return write("creation",
                         {{"action", "edit"},
                          {"operation", "patch"},
                          {"data", QJsonObject{{"kind", kind}, {"id", objectId}, {"changes", changes}}}});
        };
        auto capture = [&](QString name) {
            settle();
            auto result = call("screenshot", {});
            auto blocks = result["content"].toArray();
            QImage image;
            check(image.loadFromData(
                      QByteArray::fromBase64(blocks[0].toObject()["data"].toString().toLatin1())),
                  "MCP screenshot decoded");
            check(image.save(out + "/" + name + ".png"), "screenshot saved");
        };
        wb::StructureAsset tree;
        tree.id = "fixture-tree";
        tree.name = "验收树 · 根部枢轴";
        tree.width = 7;
        tree.height = 10;
        tree.depth = 7;
        tree.pivot = {3, 0, 3};
        for (int y = 0; y < 7; ++y)
            tree.blocks.push_back({3, y, 3, "minecraft:oak_log[axis=y]"});
        for (int y = 5; y < 10; ++y)
            for (int z = 0; z < 7; ++z)
                for (int x = 0; x < 7; ++x)
                    if ((x - 3) * (x - 3) + (z - 3) * (z - 3) <= (y == 9 ? 4 : 10) &&
                        !(x == 3 && z == 3 && y < 7))
                        tree.blocks.push_back({x, y, z, "minecraft:oak_leaves[persistent=true]"});
        auto treePath = jsonFile("tree.wbasset", {{"version", 1}, {"asset", tree.toJson()}});
        write("creation", {{"action", "import_asset"}, {"path", treePath}});
        QString treeId = editor.world().creation.assets.back().id;
        wb::PcgGraph graph;
        graph.id = "landscape";
        graph.name = "地表着色 + 植被";
        graph.nodes = {
            {"noise", "noise", {{"scale", 18}, {"octaves", 3}, {"uiX", 45}, {"uiY", 45}}},
            {"surface",
             "output_surface",
             {{"block", "minecraft:stone"},
              {"under", "minecraft:dirt"},
              {"depth", 2},
              {"threshold", .54},
              {"uiX", 360},
              {"uiY", 45}}},
            {"density", "parameter", {{"name", "density"}, {"default", .8}, {"uiX", 45}, {"uiY", 260}}},
            {"trees",
             "output_scatter",
             {{"assetId", treeId}, {"spacing", 12}, {"density", .85}, {"uiX", 360}, {"uiY", 260}}}};
        graph.connections = {{"noise", "value", "surface", "value"}, {"density", "value", "trees", "value"}};
        add("graphs", graph.toJson());
        wb::PcgVolume volume;
        volume.id = "forest-bounds";
        volume.name = "森林 · 范围与过渡";
        volume.graphId = graph.id;
        volume.min = {-48, 0, -48};
        volume.max = {48, 60, 48};
        volume.transition = 8;
        volume.seed = 21;
        add("volumes", volume.toJson());
        settle();
        check(editor.world().generated->scatterPoints.size() > 5, "MCP graph scatters real structures");
        auto generated = editor.world().generated->cells;
        auto points = editor.world().generated->scatterPoints;
        auto stateBefore = editor.world().creation;
        auto revision = editor.revision();
        auto stale =
            request("tools/call", {{"name", "creation"},
                                   {"arguments", QJsonObject{{"action", "new_graph"},
                                                             {"expected_revision", double(revision - 1)}}}});
        check(stale["isError"].toBool() && editor.revision() == revision &&
                  editor.world().creation == stateBefore,
              "MCP stale revision rejects atomic mutation");
        auto cycle = graph;
        cycle.nodes = {{"a", "add", {}}, {"b", "multiply", {}}};
        cycle.connections = {{"a", "value", "b", "a"}, {"b", "value", "a", "a"}};
        auto cyclic =
            request("tools/call", {{"name", "creation"},
                                   {"arguments", QJsonObject{{"action", "edit"},
                                                             {"operation", "graph"},
                                                             {"data", cycle.toJson()},
                                                             {"expected_revision", double(revision)}}}});
        check(cyclic["isError"].toBool() && editor.revision() == revision &&
                  editor.world().creation == stateBefore,
              "cyclic graph rejected atomically through MCP");
        lastFailure.clear();
        if (auto *dialog = window->findChild<QObject *>("errorDialog"))
            QMetaObject::invokeMethod(dialog, "close");
        patch("volumes", volume.id, {{"overrides", QJsonObject{{"density", 0}}}});
        check(editor.world().generated->scatterPoints.empty(), "instance override removes scatter");
        write("undo", {});
        check(editor.world().generated->cells == generated, "undo restores same deterministic forest");
        patch("volumes", volume.id, {{"visible", false}});
        check(editor.world().generated->cells.empty(), "visibility restores base");
        write("undo", {});
        call("camera", {{"position", QJsonArray{105, 100, 140}}, {"yaw", -36}, {"pitch", -27}});
        editor.setSelectedNode(volume.id);
        capture("01-pcg-scene");
        auto *workspace = window->findChild<QObject *>("creationWorkspace");
        auto *panel = window->findChild<QObject *>("creationPanel");
        check(workspace && panel, "creation workspace and tabs exist");
        panel->setProperty("graphId", graph.id);
        QMetaObject::invokeMethod(workspace, "open");
        QTest::qWait(350);
        auto *graphUi = window->findChild<QObject *>("pcgGraphEditor");
        check(graphUi, "node editor exists");
        graphUi->setProperty("selected", "noise");
        const auto graphBaseline = editor.world().generated->cells;
        patch("volumes", volume.id, {{"overrides", QJsonObject{{"density", 0}}}});
        // The generation popup restores focus when its close transition ends.
        // Wait for it before assigning keyboard focus to the node canvas.
        QTest::qWait(200);
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "graph workspace active before shortcut input");
        qobject_cast<QQuickItem *>(graphUi)->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        check(QTest::qWaitFor([&] { return editor.world().generated->cells == graphBaseline; }, 15000),
              "Ctrl+Z works once inside embedded graph workspace");

        QTest::qWait(250);
        check(window->grabWindow().save(out + "/02-node-graph.png"), "node editor reference capture");
        panel->setProperty("assetId", treeId);
        auto *tabs = window->findChild<QObject *>("creationTabs");
        tabs->setProperty("currentIndex", 1);
        auto *inspector = editor.assetInspector();
        check(QTest::qWaitFor([&] { return !inspector->meshBusy() && inspector->scene()->vertices > 0; },
                              30000),
              "asset inspector meshes real blocks");
        auto *assetViewport = window->findChild<Viewport *>("assetPreviewViewport");
        check(assetViewport, "asset GPU viewport present");
        check(assetViewport->camera().y() < 0, "asset camera frames below-zero inspection origin");
        for (const auto &chunk : inspector->scene()->chunks)
            for (const auto &vertex : chunk->opaque)
                check(vertex.y >= -63.f, "asset inspector excludes surrounding bedrock");

        QTest::qWait(350);
        check(window->grabWindow().save(out + "/03-asset-editor.png"), "asset tab reference capture");
        QMetaObject::invokeMethod(workspace, "close");
        auto *shelf = window->findChild<QQuickItem *>("assetShelf");
        auto *shelfGrid = window->findChild<QQuickItem *>("assetShelfGrid");
        check(shelf && shelfGrid, "bottom asset library available");
        shelf->setProperty("expanded", true);
        call("camera", {{"position", QJsonArray{0, 70, 80}}, {"yaw", 0}, {"pitch", -40}});
        settle();
        check(shelfGrid->property("count").toInt() > 0, "asset library shows imported assets");
        const auto dragStart = shelfGrid->mapToScene({30, 30}).toPoint();
        const auto dragEnd = viewport->mapToScene({viewport->width() * .5, viewport->height() * .5}).toPoint();
        const auto beforeDrag = editor.world().creation.presets.size();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(window, dragStart + QPoint(15, -15), 30);
        QTest::mouseMove(window, dragEnd, 80);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        check(QTest::qWaitFor([&] { return editor.world().creation.presets.size() == beforeDrag + 1; },
                              15000),
              "pointer drag from bottom library places a real structure on the terrain");
        capture("03b-asset-library");
        write("undo", {});
        check(editor.world().creation.presets.size() == beforeDrag, "asset library drop is undoable");
        shelf->setProperty("expanded", false);
        write("creation", {{"action", "place_asset"}, {"asset_id", treeId}, {"x", 52}, {"y", 25}, {"z", 0}});
        QString placedId = editor.world().creation.presets.back().id;
        patch("presets", placedId, {{"rotation", QJsonArray{0, 90, 0}}});
        check(editor.world().palette[editor.world().block(52, 25, 0)].name == "minecraft:oak_log",
              "rotated root remains fixed");
        auto presetPath = out + "/portable-tree.wbasset";
        write("creation",
              {{"action", "file"}, {"operation", "save_asset"}, {"id", placedId}, {"path", presetPath}});
        write("creation", {{"action", "import_asset"}, {"path", presetPath}});
        check(editor.world().creation.assets.back().defaultRotation.y() == 90,
              "portable preset restores transform default");
        add("roadNodes",
            QJsonObject{{"id", "road-a"}, {"name", "道路 A"}, {"position", QJsonArray{-45, 25, 0}}});
        add("roadNodes",
            QJsonObject{{"id", "road-b"}, {"name", "道路 B"}, {"position", QJsonArray{45, 25, 0}}});
        add("roadNodes",
            QJsonObject{{"id", "road-c"}, {"name", "道路 C"}, {"position", QJsonArray{0, 25, -45}}});
        wb::RoadEdge road;
        road.id = "road-ab";
        road.from = "road-a";
        road.to = "road-b";
        road.width = 5;
        add("roadEdges", road.toJson());
        road.id = "road-bc";
        road.from = "road-c";
        add("roadEdges", road.toJson());
        check(editor.world().palette[editor.world().block(0, editor.world().height(0, 0), 0)].name ==
                  "minecraft:gravel",
              "road surface preserved against PCG paint");
        editor.setSelectedNode("road-b");
        capture("04-road-network");
        wb::StructureAsset cave;
        cave.id = "fixture-cave";
        cave.name = "岩洞 · 入口标记";
        cave.width = 11;
        cave.height = 8;
        cave.depth = 13;
        cave.pivot = {5, 0, 0};
        for (int y = 0; y < cave.height; ++y)
            for (int z = 0; z < cave.depth; ++z)
                for (int x = 0; x < cave.width; ++x)
                    if ((x == 0 || x == 10 || y == 0 || y == 7 || z == 0 || z == 12) &&
                        !(z == 0 && x >= 4 && x <= 6 && y >= 1 && y <= 4))
                        cave.blocks.push_back({x, y, z, "minecraft:stone"});
        auto cavePath = jsonFile("cave.wbasset", {{"version", 1}, {"asset", cave.toJson()}});
        write("creation", {{"action", "import_asset"}, {"path", cavePath}});
        QString caveId = editor.world().creation.assets.back().id;
        add("caveMarkers", QJsonObject{{"id", "cave-entry"},
                                       {"assetId", caveId},
                                       {"kind", "portal"},
                                       {"position", QJsonArray{4, 1, 0}},
                                       {"end", QJsonArray{6, 4, 0}}});
        add("caveMarkers", QJsonObject{{"id", "cave-interior"},
                                       {"assetId", caveId},
                                       {"kind", "interior"},
                                       {"position", QJsonArray{5, 3, 5}}});
        check(editor.caveAnalysis(caveId)["valid"].toBool(), "cave six-face closure accepted");
        patch("assets", caveId, {{"placement", "cave"}});
        write("creation", {{"action", "file"},
                           {"operation", "save_asset"},
                           {"id", caveId},
                           {"path", out + "/cave-marked.wbasset"}});
        write("creation", {{"action", "place_asset"}, {"asset_id", caveId}, {"x", 0}, {"y", 12}, {"z", 45}});
        check(editor.world().block(0, 15, 50) == wb::Air && editor.world().block(0, 15, 44) == wb::Stone,
              "cave carves interior preserves exterior");
        panel->setProperty("assetId", caveId);
        QMetaObject::invokeMethod(workspace, "open");
        QTest::qWait(250);
        auto *assetEditor = window->findChild<QObject *>("assetEditor");
        auto *sliceY = window->findChild<QObject *>("assetSliceY");
        auto *showAnalysis = window->findChild<QObject *>("assetShowAnalysis");
        check(assetEditor && sliceY && showAnalysis, "cave analysis controls present");
        assetEditor->setProperty("analysis", editor.caveAnalysis(caveId));
        showAnalysis->setProperty("checked", true);
        sliceY->setProperty("value", 3);
        QMetaObject::invokeMethod(assetEditor, "refreshSlice");
        check(QTest::qWaitFor([&] { return !inspector->meshBusy(); }, 30000), "cave asset preview settles");
        QTest::qWait(250);
        check(window->grabWindow().save(out + "/05-cave-markers.png"), "cave asset reference capture");
        QMetaObject::invokeMethod(workspace, "close");
        call("camera", {{"position", QJsonArray{0, 16, 47}}, {"yaw", 180}, {"pitch", 0}});
        capture("06-cave-interior");
        auto project = out + "/creation-demo.wbench";
        check(editor.save(QUrl::fromLocalFile(project)), "save creation project");
        auto beforeOpen = editor.world().generated->cells;
        check(editor.open(QUrl::fromLocalFile(project)), "reopen creation project");
        settle();
        check(editor.world().generated->cells == beforeOpen, "save reopen identical authoritative voxels");
        call("camera", {{"position", QJsonArray{100, 400, 140}}, {"yaw", -35}, {"pitch", -60}});
        settle();
        check(editor.scene()->exactArea.isEmpty(), "high camera has no screen-centre precision island");
        capture("07-high-altitude");
        call("camera", {{"position", QJsonArray{0, 30, 35}}, {"yaw", 0}, {"pitch", -20}});
        settle();
        check(!editor.scene()->exactArea.isEmpty(), "close camera restores exact detail");
        auto *list = qobject_cast<QQuickItem *>(window->findChild<QObject *>("sceneNodeList"));
        check(list, "outliner present");
        list->setProperty("contentY", 34.0);
        QMetaObject::invokeMethod(list, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, -300));
        QTest::qWait(30);
        qreal contentY = list->property("contentY").toReal();
        int row = -1;
        check(QMetaObject::invokeMethod(list, "indexAt", Q_RETURN_ARG(int, row),
                                        Q_ARG(qreal, 60), Q_ARG(qreal, contentY + 17)) && row >= 0,
              "outliner click target comes from actual row geometry");
        QString expected = editor.sceneNodes().at(row).toMap()["id"].toString();
        auto point = list->mapToScene(QPointF(60, 17));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        check(editor.selectedNode() == expected, "click during inertia selects visible row immediately");
        if (editor.world().assets) {
            QString exportError;
            auto result = wb::exportMinecraft(editor.world(), out + "/Minecraft-creation");
            check(result.ok, "creation exports to Java 1.20.1");
        }
        const auto portableGraph = out + "/landscape.wbpcg";
        write("creation",
              {{"action", "file"}, {"operation", "save_graph"}, {"id", graph.id}, {"path", portableGraph}});
        // A second real stdio client cancels the synchronous command while its
        // worker runs. No direct call to cancelCreation substitutes for MCP.
        check(editor.createWorld(1024, 731, 24, 0, 8, true, 1024), "create cancellation workload");
        settle();
        check(editor.world().creation.assets.empty(), "new world starts with no scene assets");
        write("creation", {{"action", "file"}, {"operation", "load_graph"}, {"path", portableGraph}});
        check(editor.world().creation.assets.size() == 1 && editor.world().creation.graphs.size() == 1,
              "portable graph restores referenced assets in fresh project");
        auto portableVolume = volume;
        portableVolume.id = "portable-volume";
        portableVolume.graphId = editor.world().creation.graphs.front().id;
        add("volumes", portableVolume.toJson());
        check(!editor.world().generated->scatterPoints.empty(),
              "portable graph remaps scatter asset IDs correctly");
        write("undo", {});
        wb::PcgGraph heavy;
        heavy.id = "cancel-graph";
        heavy.nodes = {
            {"noise", "noise", {{"octaves", 8}, {"scale", 20}}},
            {"surface", "output_surface", {{"block", "minecraft:stone"}, {"depth", 1}, {"threshold", 0}}}};
        heavy.connections = {{"noise", "value", "surface", "value"}};
        add("graphs", heavy.toJson());
        QProcess second;
        second.start(config["command"].toString(), bridgeArgs);
        check(second.waitForStarted(5000), "second MCP client starts");
        second.write(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-"
            "06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"cancel-test\",\"version\":\"1\"}}}\n");
        QByteArray secondOutput;
        check(QTest::qWaitFor(
                  [&] {
                      secondOutput += second.readAllStandardOutput();
                      return secondOutput.contains('\n');
                  },
                  5000),
              "second MCP initialized");
        secondOutput.clear();
        second.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        bool cancelSent = false;
        auto cancelConnection = QObject::connect(&editor, &Editor::changed, &app, [&] {
            if (editor.creationBusy() && !cancelSent) {
                cancelSent = true;
                second.write(
                    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
                    "call\",\"params\":{\"name\":\"creation\",\"arguments\":{\"action\":\"cancel\"}}}\n");
            }
        });
        wb::PcgVolume workload;
        workload.id = "cancel-volume";
        workload.graphId = heavy.id;
        workload.min = {-500, 0, -500};
        workload.max = {500, 80, 500};
        const auto beforeCancel = editor.world().creation;
        const auto cancelRevision = editor.revision();
        auto cancelled =
            request("tools/call",
                    {{"name", "creation"},
                     {"arguments",
                      QJsonObject{{"action", "edit"},
                                  {"operation", "add"},
                                  {"data", QJsonObject{{"kind", "volumes"}, {"changes", workload.toJson()}}},
                                  {"expected_revision", double(cancelRevision)}}}});
        QObject::disconnect(cancelConnection);
        check(cancelSent && cancelled["isError"].toBool() && editor.revision() == cancelRevision &&
                  editor.world().creation == beforeCancel,
              "MCP cancel keeps authoritative revision and scene unchanged");
        lastFailure.clear();
        if (auto *dialog = window->findChild<QObject *>("errorDialog"))
            QMetaObject::invokeMethod(dialog, "close");
        check(QTest::qWaitFor(
                  [&] {
                      secondOutput += second.readAllStandardOutput();
                      return secondOutput.contains('\n');
                  },
                  5000),
              "MCP cancellation acknowledgment received");
        second.closeWriteChannel();
        check(second.waitForFinished(5000), "second bridge closes");
        if (!source.isEmpty()) {
            const QString copy = out + "/large-project-copy.wbench";
            check(QFile::copy(source, copy), "copy user project without overwriting original");
            check(editor.open(QUrl::fromLocalFile(copy)), "open user large project copy");
            settle();
            write("creation", {{"action", "import_asset"}, {"path", treePath}});
            const auto largeTree = editor.world().creation.assets.back().id;
            auto largeGraph = graph;
            largeGraph.id = "large-graph";
            largeGraph.nodes.back().parameters["assetId"] = largeTree;
            add("graphs", largeGraph.toJson());
            auto localVolume = volume;
            localVolume.id = "large-volume";
            localVolume.graphId = largeGraph.id;
            int dryX = 0, dryZ = 0, dryHeight = editor.world().seaLevel;
            double best = -1e9;
            const auto &largeWorld = editor.world();
            for (int z = largeWorld.originZ + 128; z < largeWorld.originZ + largeWorld.depth - 128; z += 64)
                for (int x = largeWorld.originX + 128; x < largeWorld.originX + largeWorld.width - 128;
                     x += 64) {
                    const int h = largeWorld.height(x, z);
                    if (h <= largeWorld.seaLevel + 8 || h > 270)
                        continue;
                    double slope = std::abs(largeWorld.height(x + 32, z) - largeWorld.height(x - 32, z)) +
                                   std::abs(largeWorld.height(x, z + 32) - largeWorld.height(x, z - 32));
                    double score = h - slope * 2;
                    if (score > best) {
                        best = score;
                        dryX = x;
                        dryZ = z;
                        dryHeight = h;
                    }
                }
            localVolume.min = {float(dryX - 64), -63, float(dryZ - 64)};
            localVolume.max = {float(dryX + 64), 320, float(dryZ + 64)};
            add("volumes", localVolume.toJson());
            settle();
            check(!editor.world().generated->scatterPoints.empty(),
                  "large project dryland scatter generated");
            call("camera", {{"position", QJsonArray{dryX + 150, dryHeight + 140, dryZ + 220}},
                            {"yaw", -35},
                            {"pitch", -30}});
            capture("08-large-project-pcg");
            check(editor.save(QUrl::fromLocalFile(copy)), "save modified copy");
            auto resultBefore = editor.world().generated->cells;
            check(editor.open(QUrl::fromLocalFile(copy)), "reopen modified large project");
            settle();
            check(resultBefore == editor.world().generated->cells, "large project creation is reproducible");
            check(digest(source) == sourceHash, "user original SHA256 unchanged");
        }
        if (!skyFile.isEmpty()) {
            check(editor.open(QUrl::fromLocalFile(project)), "open example for real HDR sky verification");
            settle();
            editor.setSelectedNode("environment-sky");
            auto *skyDialog = window->findChild<QObject *>("skyFileDialog");
            check(skyDialog, "actual sky file dialog exists");
            skyDialog->setProperty("selectedFile", QUrl::fromLocalFile(skyFile));
            check(QMetaObject::invokeMethod(skyDialog, "accepted"),
                  "trigger real QML file selection handler");
            check(editor.world().environment.skyTexturePath == skyFile,
                  "file URL becomes a valid local HDR path");
            write(
                "environment",
                {{"changes",
                  QJsonObject{{"enabled", true}, {"sky", true}, {"clouds", false}, {"atmosphere", false}}}});
            call("camera", {{"position", QJsonArray{90, 70, 115}}, {"yaw", -35}, {"pitch", -15}});
            capture("09-hdr-sky");
        }
        window->resize(1100, 680);
        check(!editor.graphData(panel->property("graphId").toString()).isEmpty() &&
                  !editor.assetData(panel->property("assetId").toString()).isEmpty(),
              "persistent creation tabs select valid graph and asset after project replacement");
        window->setProperty("workspacePage", 0);
        editor.setTool(0);
        QTest::qWait(150);
        check(viewport->width() >= 400 && viewport->height() >= 250,
              "minimum window leaves a usable scene viewport with brush and inspector");
        check(window->grabWindow().save(out + "/10-compact-scene.png"), "compact scene capture");
        for (int page : {1, 2, 3}) {
            window->setProperty("workspacePage", page);
            QTest::qWait(100);
            auto *creationItem = qobject_cast<QQuickItem *>(panel);
            check(creationItem->width() >= 1000 && creationItem->height() >= 450,
                  "creation tabs use full available workspace at minimum window size");
            check(window->grabWindow().save(out + "/10-compact-" + QString::number(page) + ".png"),
                  "compact creation workspace capture");
        }
        window->setProperty("workspacePage", 0);
        QFile transcriptFile(out + "/mcp-transcript.json");
        check(transcriptFile.open(QIODevice::WriteOnly), "transcript writable");
        transcriptFile.write(QJsonDocument(transcript).toJson());
        bridge.closeWriteChannel();
        check(bridge.waitForFinished(5000), "bridge clean shutdown");
        std::cout << "Creation UI tests passed: " << checks << "; output=" << out.toStdString() << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << std::endl;
        return 1;
    }
}
