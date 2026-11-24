#include "Editor.h"
#include "Viewport.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItemGrabResult>
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
        if (args.contains("--placement-latency")) {
            check(editor.open(QUrl::fromLocalFile(source)), "open placement benchmark");
            settle();
            const auto state = editor.world().creation;
            const auto cave = std::find_if(state.assets.begin(), state.assets.end(),
                                           [](const auto &a) { return a.placement == "cave"; });
            check(cave != state.assets.end(), "benchmark has a closed shell");
            const auto placed = std::find_if(state.presets.begin(), state.presets.end(),
                                             [&](const auto &p) { return p.assetId == cave->id; });
            check(placed != state.presets.end(), "benchmark has a placed cave");
            const auto start = placed->position;
            call("camera", {{"position", QJsonArray{start.x() + 58, start.y() + 40, start.z() + 90}},
                            {"yaw", -32},
                            {"pitch", -18}});
            editor.setExactRadiusChunks(1);
            settle();
            QJsonObject report;
            auto sample = [&](const wb::AssetPreset &p, QString name, QVector3D delta) {
                editor.setSelectedNode(p.id);
                const auto eye =
                    p.position + (name == "near_building" ? QVector3D(8, 6, 15) : QVector3D(58, 40, 90));
                call("camera",
                     {{"position", QJsonArray{eye.x(), eye.y(), eye.z()}}, {"yaw", -32}, {"pitch", -18}});
                settle();
                QJsonArray samples;
                for (int n = 0; n < 12; ++n) {
                    auto step = (n % 2 ? -1 : 1) * delta;
                    editor.beginCreationTransform();
                    editor.previewCreationTransform({{"x", step.x()}, {"y", step.y()}, {"z", step.z()}});
                    QTest::qWait(16);
                    auto base = editor.world().generated->procedural;
                    QElapsedTimer timer;
                    timer.start();
                    editor.endCreationTransform();
                    auto ms = [&]() { return timer.nsecsElapsed() / 1e6; };
                    double commit = ms();
                    while (editor.meshBusy() && ms() < 20000)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
                    double convergence = ms();
                    check(!editor.meshBusy() && !editor.assetBusy(), "placement converged");
                    auto scene = editor.scene();
                    const auto metrics = viewport->renderMetrics();
                    auto swapped = std::make_shared<std::atomic_bool>(false);
                    auto connection = QObject::connect(
                        window, &QQuickWindow::frameSwapped, window,
                        [swapped, metrics, scene] {
                            if (metrics->renderedSourceRevision == scene->sourceRevision &&
                                metrics->renderedMeshRevision == scene->revision)
                                swapped->store(true);
                        },
                        Qt::DirectConnection);
                    viewport->update();
                    while (!swapped->load() && ms() < 20000)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
                    double presented = ms();
                    QObject::disconnect(connection);
                    check(swapped->load(), "presented converged placement");
                    samples.append(QJsonObject{
                        {"generation_ms", editor.lastPlacementGenerationMs()},
                        {"commit_ms", commit},
                        {"scene_ms", convergence},
                        {"present_ms", presented},
                        {"reused_procedural", base && base == editor.world().generated->procedural},
                        {"updated_far", scene->updatedFarTiles},
                        {"updated_exact", scene->updatedChunks},
                        {"worker_ms", scene->buildMilliseconds}});
                    if (n == 0 || n == 11) {
                        auto baked = editor.world();
                        QString error;
                        check(baked.rebuildCreation(error) &&
                                  baked.generated->cells == editor.world().generated->cells &&
                                  baked.generated->objectCells == editor.world().generated->objectCells &&
                                  baked.generated->caveAir == editor.world().generated->caveAir &&
                                  baked.generated->geometryCells == editor.world().generated->geometryCells &&
                                  baked.generated->blockEntities == editor.world().generated->blockEntities &&
                                  baked.generated->entities == editor.world().generated->entities &&
                                  baked.generated->surfaceHoles == editor.world().generated->surfaceHoles,
                              "placement agrees with complete independent regeneration");
                    }
                }
                report.insert(name, samples);
                jsonFile("latency.json", report);
            };
            sample(*placed, "cave", {4, 5, 0});
            auto structure = *cave;
            structure.id = "latency-building";
            structure.name = "测试建筑实体壳";
            structure.placement = "place";
            structure.groundBlend = 0;
            add("assets", structure.toJson());
            auto building = *placed;
            building.id = "latency-building-instance";
            building.assetId = structure.id;
            int x = qRound(start.x()) + 96, z = qRound(start.z());
            building.position = {float(x), float(editor.world().height(x, z) - structure.pivot.y() + 1),
                                 float(z)};
            add("presets", building.toJson());
            settle();
            sample(building, "rigid_building", {4, 5, 0});
            sample(building, "near_building", {4, 5, 0});
            patch("presets", building.id, {{"scale", QJsonArray{2, 2, 2}}});
            settle();
            sample(building, "scaled_building", {4, 5, 0});
            editor.undo();
            settle();
            editor.redo();
            settle();
            const auto saved = out + "/moved.wbench";
            const auto expected = editor.world().generated;
            check(editor.save(QUrl::fromLocalFile(saved)) && editor.open(QUrl::fromLocalFile(saved)),
                  "reopen moved placements");
            settle();
            check(editor.world().generated->cells == expected->cells &&
                      editor.world().generated->entities == expected->entities,
                  "saving and reopening reproduces placement state");
            editor.setSelectedNode(building.id);
            const auto eye = building.position + QVector3D(23, 15, 32);
            call("camera", {{"position", QJsonArray{eye.x(), eye.y(), eye.z()}}, {"yaw", -36}, {"pitch", -18}});
            capture("placements");
            check(digest(source) == sourceHash, "preserved placement benchmark source");
            return 0;
        }
        if (args.contains("--move-review")) {
            check(!source.isEmpty() && editor.open(QUrl::fromLocalFile(source)),
                  "open the user project copy");
            settle();
            auto state = editor.world().creation;
            auto placed = std::find_if(state.presets.begin(), state.presets.end(), [&](const auto &p) {
                return std::any_of(state.assets.begin(), state.assets.end(), [&](const auto &a) {
                    return a.id == p.assetId && a.placement == "cave" && p.enabled;
                });
            });
            check(placed != state.presets.end(), "review includes an imported closed shell");
            auto oldScene = editor.scene();
            const auto row = std::find_if(oldScene->objects.begin(), oldScene->objects.end(),
                                          [&](const auto &o) { return o.id == placed->id; });
            check(row != oldScene->objects.end() && !row->lods.empty(), "user shell has derived meshes");
            QJsonArray triangles{int(row->mesh->opaque.size() / 3)};
            for (const auto &lod : row->lods)
                triangles.append(int(lod->opaqueIndices.size() / 3));
            check(triangles.last().toInt() < triangles.first().toInt(), "closed shell reduces triangles");
            auto pos = placed->position;
            editor.setSelectedNode(placed->id);
            call("camera", {{"position", QJsonArray{pos.x() + 58, pos.y() + 40, pos.z() + 90}},
                            {"yaw", -32},
                            {"pitch", -18}});
            editor.setExactRadiusChunks(1);
            capture("closed-shell");
            auto profile = [&](QString kind, QString id, QJsonObject changes) {
                settle();
                auto baseline = editor.scene();
                QElapsedTimer timer;
                timer.start();
                patch(kind, id, changes);
                auto commit = timer.elapsed();
                settle();
                int stable = 0;
                QSet<const wb::ChunkMesh *> meshes;
                for (const auto &o : baseline->objects)
                    meshes.insert(o.mesh.get());
                for (const auto &o : editor.scene()->objects)
                    stable += meshes.contains(o.mesh.get());
                auto data = QJsonObject{{"command_ms", commit},
                                        {"settled_ms", timer.elapsed()},
                                        {"reused_original_meshes", stable},
                                        {"objects", int(editor.scene()->objects.size())},
                                        {"updated_far_tiles", editor.scene()->updatedFarTiles},
                                        {"updated_exact_chunks", editor.scene()->updatedChunks}};
                write("undo", {});
                settle();
                return data;
            };
            auto moved =
                profile("presets", placed->id, {{"position", QJsonArray{pos.x() + 4, pos.y() + 5, pos.z()}}});
            QJsonObject volumeTime;
            if (!state.volumes.empty()) {
                auto vol = *std::max_element(
                    state.volumes.begin(), state.volumes.end(), [](const auto &a, const auto &b) {
                        return (a.max - a.min).lengthSquared() < (b.max - b.min).lengthSquared();
                    });
                volumeTime = profile("volumes", vol.id,
                                     {{"min", QJsonArray{vol.min.x() + 4, vol.min.y(), vol.min.z()}},
                                      {"max", QJsonArray{vol.max.x() + 4, vol.max.y(), vol.max.z()}}});
            }
            jsonFile("move-review.json",
                     {{"shell_triangles", triangles}, {"shell_move", moved}, {"range_move", volumeTime}});
            check(digest(source) == sourceHash, "preserve review project");
            return 0;
        }
        if (args.contains("--forest-review")) {
            check(!source.isEmpty() && editor.open(QUrl::fromLocalFile(source)), "open review project copy");
            settle();
            auto inspect = call("creation", {{"action", "inspect"}});
            auto state = QJsonDocument::fromJson(
                             inspect["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                             .object()["creation"]
                             .toObject();
            QJsonArray results;
            QVector3D caveEye, caveDirection;
            for (const auto value : state["presets"].toArray()) {
                wb::AssetPreset placement;
                QString error;
                check(wb::AssetPreset::fromJson(value.toObject(), placement, error), "read MCP placement");
                for (const auto &asset : editor.world().creation.assets) {
                    if (asset.id != placement.assetId || asset.placement != "cave")
                        continue;
                    const auto analysis = wb::analyzeAssetCave(asset, editor.world().creation.caveMarkers);
                    // Audit the interior cells of a unit-scale, quarter-turn placement.
                    if (placement.scale != QVector3D(1, 1, 1))
                        continue;
                    const auto matrix = wb::placementMatrix(placement);
                    QVector3D lo(1e8, 1e8, 1e8), hi(-1e8, -1e8, -1e8);
                    std::vector<QVector3D> cells;
                    QVector3D chamber, portals;
                    int inside = 0, openings = 0;
                    for (int y = 0; y < asset.height; ++y)
                        for (int z = 0; z < asset.depth; ++z)
                            for (int x = 0; x < asset.width; ++x) {
                                auto label = analysis.labels[(y * asset.depth + z) * asset.width + x];
                                if (label != 1 && label != 2)
                                    continue;
                                auto pos = matrix.map(QVector3D(x + .5f, y + .5f, z + .5f) - asset.pivot);
                                for (int i = 0; i < 3; ++i) {
                                    pos[i] = std::floor(pos[i]);
                                    lo[i] = std::min(lo[i], pos[i]);
                                    hi[i] = std::max(hi[i], pos[i]);
                                }
                                cells.push_back(pos);
                                if (label == 2) {
                                    portals += pos;
                                    ++openings;
                                } else {
                                    chamber += pos;
                                    ++inside;
                                }
                            }
                    if (inside && openings) {
                        const auto portal = portals / openings + QVector3D(.5f, .5f, .5f);
                        caveDirection = (chamber / inside + QVector3D(.5f, .5f, .5f) - portal).normalized();
                        caveEye = portal + caveDirection * .25f;
                    }
                    if (cells.empty())
                        continue;
                    auto region = call("read_region", {{"min", QJsonArray{lo.x(), lo.y(), lo.z()}},
                                                       {"max", QJsonArray{hi.x(), hi.y(), hi.z()}}});
                    auto blocks = QJsonDocument::fromJson(
                                      region["content"].toArray()[0].toObject()["text"].toString().toUtf8())
                                      .object();
                    const auto values = blocks["blocks"].toArray(), palette = blocks["palette"].toArray();
                    int solids = 0;
                    for (auto pos : cells) {
                        auto v = pos - lo;
                        int idx =
                            (int(v.y()) * int(hi.z() - lo.z() + 1) + int(v.z())) * int(hi.x() - lo.x() + 1) +
                            int(v.x());
                        if (!palette[values[idx].toInt()].toString().endsWith("air"))
                            ++solids;
                    }
                    results.append(QJsonObject{{"asset", asset.name},
                                               {"interior_and_portal", int(cells.size())},
                                               {"solid", solids}});
                }
            }
            call("camera", {{"position", QJsonArray{100, 235, -225}}, {"yaw", -32}, {"pitch", -25}});
            capture("forest");
            std::vector<double> frameTimes, gpuTimes, cpuTimes;
            QElapsedTimer timer;
            timer.start();
            qint64 last = 0;
            auto connection = QObject::connect(
                window, &QQuickWindow::frameSwapped, &app,
                [&] {
                    auto now = timer.nsecsElapsed();
                    if (last)
                        frameTimes.push_back((now - last) / 1e6);
                    last = now;
                    gpuTimes.push_back(viewport->renderMetrics()->gpuMicros.load() / 1000.);
                    cpuTimes.push_back(viewport->renderMetrics()->submitMicros.load() / 1000.);
                },
                Qt::QueuedConnection);
            while (frameTimes.size() < 120 && timer.elapsed() < 45000) {
                viewport->update();
                QTest::qWait(4);
            }
            QObject::disconnect(connection);
            check(frameTimes.size() >= 100, "measure forest frames");
            std::sort(frameTimes.begin(), frameTimes.end());
            std::sort(gpuTimes.begin(), gpuTimes.end());
            std::sort(cpuTimes.begin(), cpuTimes.end());
            auto render = info()["rendering"].toObject();
            render["cpu_p50_ms"] = cpuTimes[cpuTimes.size() / 2];
            render["draws"] = viewport->renderMetrics()->draws.load();
            render["cluster_candidates"] = int(editor.scene()->clusters.size());
            render["frame_p50_ms"] = frameTimes[frameTimes.size() / 2];
            render["frame_p95_ms"] = frameTimes[frameTimes.size() * 95 / 100];
            render["gpu_p50_ms"] = gpuTimes[gpuTimes.size() / 2];
            jsonFile(
                "review.json",
                {{"caves", results}, {"render", render}, {"objects", int(editor.scene()->objects.size())}});
            if (!caveDirection.isNull()) {
                call("camera", {{"position", QJsonArray{caveEye.x(), caveEye.y(), caveEye.z()}},
                                {"yaw", qRadiansToDegrees(std::atan2(caveDirection.x(), -caveDirection.z()))},
                                {"pitch", qRadiansToDegrees(std::asin(caveDirection.y()))}});
                capture("cave");
            }
            // A known narrow summit exercises the case where the ground directly
            // below the camera is hundreds of blocks lower than the nearest cliff.
            wb::WorldOptions options;
            options.flat = true;
            options.width = options.depth = 512;
            options.baseHeight = 24;
            auto pillar = wb::World::create(options);
            for (int z = -12; z <= 12; ++z)
                for (int x = -12; x <= 12; ++x)
                    if (x * x + z * z <= 144)
                        pillar.heights.set(pillar.column(x, z), 238 - (x * x + z * z) / 28);
            QString error;
            const auto path = out + "/cliff-fixture.wbench";
            check(wb::saveProject(path, pillar, error) && editor.open(QUrl::fromLocalFile(path)),
                  "open isolated elevated terrain fixture");
            settle();
            call("camera", {{"position", QJsonArray{38, 242, 30}}, {"yaw", -52}, {"pitch", -13}});
            capture("cliff");
            check(editor.previewContains(0, 0) && editor.world().height(38, 30) == 24,
                  "lateral approach at summit altitude activates exact terrain");
            call("camera", {{"position", QJsonArray{210, 242, 30}}, {"yaw", -90}, {"pitch", -10}});
            settle();
            check(editor.scene()->exactArea.isEmpty(), "moving away at the same altitude releases exact terrain");
            check(digest(source) == sourceHash, "review leaves the supplied project intact");
            return 0;
        }
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
        auto legacyTree = tree.toJson();
        legacyTree.remove("category");
        auto treePath = jsonFile("tree.wbasset", {{"version", 1}, {"asset", legacyTree}});
        write("creation", {{"action", "import_asset"}, {"path", treePath}});
        QString treeId = editor.world().creation.assets.back().id;
        if (editor.world().assets) {
            auto log = tree;
            log.id = "thumbnail-log";
            log.name = "原木贴图";
            log.width = log.height = log.depth = 1;
            log.pivot = {};
            log.blocks.clear();
            log.blocks.push_back({0, 0, 0, "minecraft:oak_log[axis=y]"});
            auto path = jsonFile("thumbnail-log.wbasset", {{"asset", log.toJson()}});
            check(editor.importStructureAsset(QUrl::fromLocalFile(path)), "import one textured block");
            const auto uri = editor.assetThumbnail(log.id, "test");
            const auto thumbnail = QImage::fromData(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
            QSet<QRgb> colors;
            for (int y = 33; y < 43; ++y)
                for (int x = 67; x < 76; ++x)
                    colors.insert(thumbnail.pixel(x, y));
            check(colors.size() > 5, "actual JAR texels vary inside one face");
            check(thumbnail.save(out + "/BI-oak-log.png"), "capture textured oak block thumbnail");
            write("undo", {});
        }
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
        {
            auto bounds = volume;
            bounds.id = "occlusion-fixture";
            bounds.enabled = false;
            bounds.min = {-8, 4, -8};
            bounds.max = {8, 16, 8};
            bounds.transition = 0;
            call("camera", {{"position", QJsonArray{0, 60, 46}}, {"yaw", 0}, {"pitch", -35}});
            editor.setSelectedNode("base");
            auto crop = [&] {
                settle();
                auto grab = viewport->grabToImage();
                check(grab && QTest::qWaitFor([&] { return !grab->image().isNull(); }), "read bounds depth test");
                auto image = grab->image().convertToFormat(QImage::Format_RGB32);
                return image.copy(image.width() / 3, image.height() / 3, image.width() / 3, image.height() / 3);
            };
            const bool lighting = editor.world().environment.enabled;
            editor.setEnvironment({{"enabled", false}});
            add("volumes", bounds.toJson());
            const auto buried = crop();
            patch("volumes", bounds.id, {{"visible", false}});
            const auto original = crop();
            auto changedPixels = [&](const QImage &other) {
                check(other.size() == original.size(), "compare identical viewport crops");
                int count = 0;
                for (int y = 0; y < original.height(); ++y)
                    for (int x = 0; x < original.width(); ++x) {
                        const QColor a = original.pixelColor(x, y), b = other.pixelColor(x, y);
                        if (std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                                      std::abs(a.blue() - b.blue())}) > 4)
                            ++count;
                    }
                return count;
            };
            check(changedPixels(buried) == 0, "opaque ground completely occludes a buried PCG box");
            write("undo", {});
            patch("volumes", bounds.id, {{"min", QJsonArray{-8, 27, -8}}, {"max", QJsonArray{8, 38, 8}}});
            check(changedPixels(crop()) > 50, "the same bounds appear above ground");
            auto clickWorld = [&](QVector3D worldPoint) {
                const auto clip = (viewport->viewProjection() * QVector4D(worldPoint, 1)).toVector3DAffine();
                const auto pos = viewport
                                     ->mapToScene({(clip.x() + 1) * viewport->width() / 2,
                                                   (1 - clip.y()) * viewport->height() / 2})
                                     .toPoint();
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos);
            };
            editor.setSelectedNode({});
            clickWorld({0, 38, 8});
            check(editor.selectedNode() == bounds.id, "click a visible PCG edge away from its badge");
            QTest::keyClick(window, Qt::Key_Escape);
            check(editor.selectedNode().isEmpty(), "Escape clears scene selection");
            clickWorld({0, 38, 8});
            clickWorld({20, 25, 15});
            check(editor.selectedNode().isEmpty(), "empty terrain click clears instance selection");
            write("undo", {});
            write("undo", {});
            editor.setEnvironment({{"enabled", lighting}});
        }
        add("volumes", volume.toJson());
        settle();
        check(editor.world().generated->scatterPoints.size() > 5, "MCP graph scatters real structures");
        write("creation", {{"action", "store_pcg_asset"}, {"id", volume.id}});
        check(editor.world().creation.pcgAssets.size() == 1, "PCG range stored as library asset through MCP");
        const auto pcgAssetId = editor.world().creation.pcgAssets.back().id;
        const auto pcgPath = out + "/forest-range.wbasset";
        write("creation",
              {{"action", "file"}, {"operation", "save_asset"}, {"id", pcgAssetId}, {"path", pcgPath}});
        write("creation", {{"action", "import_asset"}, {"path", pcgPath}});
        check(editor.world().creation.pcgAssets.size() == 2, "portable PCG asset imported with dependencies");
        QFile rangeFile(pcgPath);
        check(rangeFile.open(QIODevice::ReadOnly), "read range manifest");
        auto manifest = QJsonDocument::fromJson(rangeFile.readAll()).object();
        check(manifest.contains("graphRecord") && !manifest.contains("assets") && !manifest.contains("graph"),
              "range stores only shared references");
        rangeFile.close();
        const auto libraryId = editor.world().creation.pcgAssets.back().id;
        write("creation",
              {{"action", "place_asset"}, {"asset_id", libraryId}, {"x", 0}, {"y", 25}, {"z", 0}});
        check(editor.world().creation.volumes.size() == 2 &&
                  editor.world().creation.volumes.back().graphId == graph.id &&
                  editor.world().creation.graphs.size() == 1 && editor.world().creation.assets.size() == 1,
              "import and placement reuse the original graph and tree");
        check(editor.world().creation.volumes.back().min.y() == volume.min.y(),
              "PCG preset keeps ground-relative vertical extent");
        write("undo", {});
        write("undo", {});
        write("undo", {});
        check(editor.world().creation.pcgAssets.empty() && editor.world().creation.volumes.size() == 1,
              "PCG library operations undo atomically");
        settle();
        {
            const auto originalName = editor.world().creation.assets.front().name;
            check(editor.createAssetFolder("创作") && editor.createAssetFolder("创作/植物") &&
                      editor.moveLibraryAsset(treeId, "创作/植物"),
                  "nested catalog folders and assignment");
            check(editor.renameLibraryAsset(treeId, "森林树"), "rename structure asset");
            check(!editor.renameLibraryAsset(treeId, graph.name) &&
                      editor.world().creation.assets.front().name == "森林树",
                  "cross-type duplicate rename rejects atomically");
            lastFailure.clear();
            if (auto *dialog = window->findChild<QObject *>("errorDialog"))
                QMetaObject::invokeMethod(dialog, "close");
            write("creation", {{"action", "import_asset"}, {"path", treePath}});
            const auto imported = editor.world().creation.assets.back().id;
            check(editor.renameLibraryAsset(imported, originalName), "released asset name available");
            write("creation", {{"action", "import_asset"}, {"path", treePath}});
            check(editor.world().creation.assets.back().name == originalName + "1",
                  "import collision gets numeric suffix");
            const auto stored = out + "/folders.wbench";
            check(editor.save(QUrl::fromLocalFile(stored)), "save catalog state");
            wb::World storedWorld;
            QString error;
            check(wb::loadProject(stored, storedWorld, error) &&
                      storedWorld.creation.assetFolders.value(treeId) == "创作/植物",
                  "folder hierarchy and renamed assets persist");
            write("undo", {});
            write("undo", {});
            check(editor.world().creation.assets.size() == 1 &&
                      editor.renameLibraryAsset(treeId, originalName),
                  "restore library fixture");
        }
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
        call("camera", {{"position", QJsonArray{0, 50, 65}}, {"yaw", 0}, {"pitch", -20}});
        capture("01a-assets-near");
        viewport->setAlphaBlending(true);
        auto grab = viewport->grabToImage();
        check(grab && QTest::qWaitFor([&] { return !grab->image().isNull(); }),
              "grab viewport on transparent backing");
        const auto pixels = grab->image().convertToFormat(QImage::Format_ARGB32);
        for (int y = 1; y < pixels.height() - 1; ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
            check(std::all_of(row + 1, row + pixels.width() - 1,
                              [](QRgb pixel) { return qAlpha(pixel) == 255; }),
                  "leaf/terrain frame has opaque resolved alpha in every interior pixel");
        }
        viewport->setAlphaBlending(false);
        const int nearTriangles = viewport->renderMetrics()->objectTriangles.load();
        check(nearTriangles > 0 && viewport->renderMetrics()->objectLods.load() == 0,
              "near assets use original block geometry");
        editor.setExactPreviewEnabled(false);
        settle();
        check(editor.scene()->exactArea.isEmpty() &&
                  viewport->renderMetrics()->objectLods.load() == int(editor.scene()->objects.size()),
              "all placed assets follow disabled exact terrain at the same close camera");
        editor.setExactPreviewEnabled(true);
        settle();
        call("camera", {{"position", QJsonArray{0, 200, 500}}, {"yaw", 0}, {"pitch", -20}});
        capture("01b-assets-distant");
        const int farTriangles = viewport->renderMetrics()->objectTriangles.load();
        check(viewport->renderMetrics()->objectLods.load() > 0 && farTriangles < nearTriangles * .7,
              "distant assets actually select simplified GPU meshes");
        std::cout << "Asset triangles near/far " << nearTriangles << " / " << farTriangles << std::endl;
        call("camera", {{"position", QJsonArray{105, 100, 140}}, {"yaw", -36}, {"pitch", -27}});
        auto *panel = window->findChild<QQuickItem *>("creationPanel");
        auto *shelf = window->findChild<QQuickItem *>("assetShelf");
        auto *shelfGrid = window->findChild<QQuickItem *>("assetShelfGrid");
        check(panel && shelf && shelfGrid, "asset workspace and scene library exist");
        {
            const auto before = editor.world().creation;
            const auto root = out + "/DragLibrary";
            check(QDir().mkpath(root + "/地形"), "create folder drop fixture");
            QImage gray(8, 8, QImage::Format_Grayscale8);
            gray.fill(Qt::black);
            check(gray.save(root + "/地形/A.png") && gray.save(root + "/地形/B.png"), "write mixed PNG drop");
            const auto fileUrl = QUrl::fromLocalFile(treePath);
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(root), fileUrl});
            QPoint pos = shelf->mapToScene({shelf->width() * .5, shelf->height() * .5}).toPoint();
            QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            check(enter.isAccepted(), "asset shelf accepts native folder/file drag");
            QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &drop);
            check(drop.isAccepted() && editor.world().heights.instances.size() == 2 &&
                      editor.world().creation.assets.size() == before.assets.size() + 1 &&
                      editor.world().creation.assetFolders.value(
                          editor.world().heights.instances.back().id) == "DragLibrary/地形",
                  "recursive native import preserves hierarchy and both heightmaps");
            settle();
            check(window->grabWindow().save(out + "/BI-library-drop.png"), "capture dropped library");
            write("undo", {});
            check(editor.world().creation == before && editor.world().heights.instances.empty(),
                  "undo whole mixed drop");
        }
        auto scene = [&] { QMetaObject::invokeMethod(window, "activate", Q_ARG(QVariant, QString{})); };
        auto cardPoint = [&](const QString &id) {
            scene();
            shelf->setProperty("expanded", true);
            check(QTest::qWaitFor([&] {
                      return shelfGrid->isVisible() && shelf->height() > 150 && shelfGrid->height() > 100;
                  }),
                  "asset drawer laid out");
            QTest::qWait(100);
            const auto rows = editor.libraryAssets();
            auto entry =
                std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return v.toMap()["id"] == id; });
            check(entry != rows.end(), "requested asset exists in shelf");
            const int index = int(entry - rows.begin());
            QMetaObject::invokeMethod(shelfGrid, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, 0));
            QQuickItem *card = nullptr;
            check(QTest::qWaitFor(
                      [&] {
                          QMetaObject::invokeMethod(shelfGrid, "itemAtIndex",
                                                    Q_RETURN_ARG(QQuickItem *, card), Q_ARG(int, index));
                          return card && card->objectName() == "libraryCard-" + id;
                      },
                      4000),
                  "asset card delegate for stable id");
            return card->mapToScene({30, 30}).toPoint();
        };
        auto openCard = [&](const QString &id) {
            const auto point = cardPoint(id);
            std::cout << "Open card " << id.toStdString() << " at " << point.x() << "," << point.y()
                      << std::endl;
            QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, point);
            const bool opened =
                QTest::qWaitFor([&] { return window->property("activeId").toString() == id; }, 3000);
            if (!opened)
                window->grabWindow().save(out + "/failed-open-card.png");
            check(opened, "double-click opens correct asset editor");
        };
        openCard(graph.id);
        openCard(graph.id);
        check(window->property("openAssetCount").toInt() == 1, "same graph reuses its tab");
        QTest::qWait(350);
        {
            auto *label = viewport->window()->contentItem()->findChild<QQuickItem *>("openTab-" + graph.id);
            // A ListView delegate is in the visual tree and may have a different QObject parent.
            std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual =
                [&](QQuickItem *item, const QString &name) -> QQuickItem * {
                    if (item->objectName() == name)
                        return item;
                    for (auto *child : item->childItems())
                        if (auto *result = findVisual(child, name))
                            return result;
                    return nullptr;
                };
            if (!label)
                label = findVisual(window->contentItem(), "openTab-" + graph.id);
            auto *close = findVisual(window->contentItem(), "closeTab-" + graph.id);
            check(label && close, "workspace tab controls present");
            auto *tab = label->parentItem()->parentItem();
            const auto point = [](QQuickItem *item) {
                return item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
            };
            QTest::mouseMove(window, QPoint(2, 2));
            QTest::qWait(50);
            const auto resting = tab->property("color");
            QTest::mouseMove(window, point(label));
            check(QTest::qWaitFor([&] { return tab->property("color") != resting; }), "tab title highlights entire tab");
            const auto hover = tab->property("color");
            QTest::mouseMove(window, point(close));
            check(QTest::qWaitFor([&] { return close->property("hovered").toBool(); }) && tab->property("color") == hover,
                  "close button shares the whole-tab hover background");
        }
        auto *graphUi = window->findChild<QObject *>("pcgGraphEditor");
        check(graphUi, "node editor exists");
        graphUi->setProperty("selected", "noise");
        const auto graphBaseline = editor.world().generated->cells;
        patch("volumes", volume.id, {{"overrides", QJsonObject{{"density", 0}}}});
        // Activate the node canvas before exercising the workspace shortcut.
        QTest::qWait(200);
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "graph workspace active before shortcut input");
        qobject_cast<QQuickItem *>(graphUi)->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        check(QTest::qWaitFor([&] { return editor.world().generated->cells == graphBaseline; }, 15000),
              "Ctrl+Z works once inside embedded graph workspace");

        QTest::qWait(250);
        check(window->grabWindow().save(out + "/02-node-graph.png"), "node editor reference capture");
        openCard(treeId);
        check(panel->property("kind") == "open", "tree opens root-pivot editor");
        check(window->property("openAssetCount").toInt() == 2, "tree opens alongside graph");
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

        auto *assetLight = window->findChild<QQuickItem *>("assetLighting");
        check(assetLight && assetLight->isVisible(), "asset light button visible");
        const auto sceneLighting = editor.world().environment.enabled;
        const auto lightPoint =
            assetLight->mapToScene({assetLight->width() / 2, assetLight->height() / 2}).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, lightPoint);
        check(inspector->world().environment.enabled && editor.world().environment.enabled == sceneLighting,
              "asset light changes inspector only");
        QTest::qWait(350);
        check(window->grabWindow().save(out + "/03-asset-editor.png"), "asset tab reference capture");
        QMetaObject::invokeMethod(window, "closeTab", Q_ARG(QVariant, treeId));
        check(window->property("inScene").toBool() && window->property("openAssetCount").toInt() == 1,
              "closing active tab restores scene and leaves the graph open");
        openCard(treeId);
        scene();
        shelf->setProperty("expanded", true);
        call("camera", {{"position", QJsonArray{0, 70, 80}}, {"yaw", 0}, {"pitch", -40}});
        settle();
        check(shelfGrid->property("count").toInt() > 0, "asset library shows imported assets");
        const auto dragStart = cardPoint(treeId);
        const auto dragEnd =
            viewport->mapToScene({viewport->width() * .5, viewport->height() * .5}).toPoint();
        const auto beforeDrag = editor.world().creation.presets.size();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(window, dragStart + QPoint(15, -15), 30);
        QTest::mouseMove(window, dragEnd, 80);
        check(QTest::qWaitFor([&] { return bool(viewport->ghostMesh()); }, 15000),
              "held library drag displays the real structure before placement");
        check(editor.world().creation.presets.size() == beforeDrag, "ghost does not commit while dragging");
        check(window->grabWindow().save(out + "/03a-live-asset-drop.png"), "capture held drag preview");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        check(
            QTest::qWaitFor([&] { return editor.world().creation.presets.size() == beforeDrag + 1; }, 15000),
            "pointer drag from bottom library places a real structure on the terrain");
        check(editor.world().creation.presets.back().name ==
                  editor.world().creation.assets.front().name + "1",
              "dropped tree takes the asset name and numeric suffix");
        capture("03b-asset-library");
        shelf->setProperty("folder", "创作/植物");
        check(QTest::qWaitFor([&] { return shelfGrid->property("count").toInt() == 1; }),
              "folder filter selects only assigned card");
        check(window->grabWindow().save(out + "/BH-folder.png"), "folder screenshot");
        shelf->setProperty("folder", QVariant::fromValue(nullptr));
        write("undo", {});
        check(editor.world().creation.presets.size() == beforeDrag, "asset library drop is undoable");
        write("creation", {{"action", "store_pcg_asset"}, {"id", volume.id}});
        settle();
        const auto savedRange = editor.world().creation.pcgAssets.back().id;
        openCard(savedRange);
        auto embeddedGraph = editor.graphData(savedRange);
        embeddedGraph["name"] = QString("edited preset graph");
        check(editor.creationEdit("graph", embeddedGraph) &&
                  editor.world().creation.pcgAssets.back().volume.graphId == graph.id &&
                  editor.world().creation.graphs.front().name == "edited preset graph",
              "range tab edits the shared graph");
        write("undo", {});
        scene();
        const auto sharedStart = cardPoint(graph.id);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, sharedStart);
        QTest::mouseMove(window, sharedStart + QPoint(16, -16), 30);
        QTest::mouseMove(window, dragEnd, 80);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        check(QTest::qWaitFor([&] { return editor.world().creation.volumes.size() == 2; }, 10000) &&
                  editor.world().creation.volumes.back().graphId == graph.id &&
                  editor.world().creation.graphs.size() == 1,
              "dragging a graph creates a linked range");
        write("undo", {});
        settle();
        const auto rangeStart = cardPoint(editor.world().creation.pcgAssets.back().id);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rangeStart);
        QTest::mouseMove(window, rangeStart + QPoint(15, -15), 30);
        QTest::mouseMove(window, dragEnd, 80);
        check(editor.world().creation.volumes.size() == 1, "PCG drag remains a preview until release");
        check(window->grabWindow().save(out + "/03c-pcg-library-preview.png"), "PCG library drag evidence");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
        check(QTest::qWaitFor([&] { return editor.world().creation.volumes.size() == 2; }, 15000),
              "PCG volume is draggable directly from the asset library");
        write("undo", {});
        write("undo", {});
        check(editor.world().creation.volumes.size() == 1 && editor.world().creation.pcgAssets.empty(),
              "PCG library drop and store undo independently");
        shelf->setProperty("expanded", false);
        write("creation", {{"action", "place_asset"}, {"asset_id", treeId}, {"x", 52}, {"y", 25}, {"z", 0}});
        QString placedId = editor.world().creation.presets.back().id;
        settle();
        // Keep the enlarged canopy inside the world for the mesh budget regression.
        patch("presets", placedId, {{"position", QJsonArray{0, 25, 0}}, {"scale", QJsonArray{8, 8, 8}}});
        settle();
        const auto &largeTree = editor.scene()->objects.back().mesh;
        check(largeTree->upper.y() - largeTree->lower.y() == 80, "8x tree retains exact height");
        std::cout << "8x tree exact mesh bytes " << largeTree->opaque.size() * sizeof(wb::Vertex)
                  << std::endl;
        call("camera", {{"position", QJsonArray{65, 85, 105}}, {"yaw", -32}, {"pitch", -15}});
        editor.setSelectedNode(placedId);
        viewport->focusTarget();
        check((viewport->orbitPivot() - largeTree->center).length() < 2,
              "focus centers transformed selected mesh");
        capture("tree-scale-eight");
        for (int frame = 0; frame < 4; ++frame) {
            call("camera",
                 {{"position", QJsonArray{65 + frame * 2, 85, 105}}, {"yaw", -32 + frame}, {"pitch", -15}});
            const int n = viewport->frameCounter()->load();
            viewport->update();
            check(QTest::qWaitFor([&] { return viewport->frameCounter()->load() > n; }) &&
                      viewport->sampleCount() == 4,
                  "moving scene renders with 4x MSAA");
        }

        write("undo", {});
        settle();
        const auto beforeCarveMesh = editor.scene()->objects.back().mesh;
        write("set_blocks", {{"blocks", QJsonArray{QJsonObject{{"position", QJsonArray{52, 26, 0}},
                                                               {"block", "minecraft:air"}}}}});
        settle();
        check(editor.scene()->objects.back().mesh != beforeCarveMesh,
              "manual block edit invalidates cached object display geometry");
        write("undo", {});
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
        openCard(caveId);
        check(panel->property("kind") == "closed", "cave opens marker editor");
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
        scene();
        call("camera", {{"position", QJsonArray{0, 16, 47}}, {"yaw", 180}, {"pitch", 0}});
        capture("06-cave-interior");
        // Every creation object uses the exact same projected gizmo and native
        // gesture path as terrain instances, including cancellation/history.
        call("camera", {{"position", QJsonArray{105, 100, 140}}, {"yaw", -36}, {"pitch", -27}});
        editor.setTool(-1);
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "gizmo window active");
        viewport->forceActiveFocus();
        auto direction = [](const QVariantMap &o, int axis) {
            for (auto a : o["axes"].toList()) {
                auto item = a.toMap();
                if (item["axis"].toInt() != axis)
                    continue;
                const auto pts = item["points"].toList();
                auto p = pts.front().toMap(), q = pts.back().toMap();
                return QPointF(q["x"].toDouble() - p["x"].toDouble(), q["y"].toDouble() - p["y"].toDouble());
            }
            return QPointF{};
        };
        auto gesture = [&](int mode, int axis, bool cancel, double amount = .65) {
            std::cout << "Gesture " << editor.selectedNode().toStdString() << " mode=" << mode
                      << " axis=" << axis << " cancel=" << cancel << std::endl;
            viewport->setGizmoMode(mode);
            const auto o = viewport->gizmoOverlay();
            check(o["active"].toBool(), "creation gizmo active");
            const QPointF center(o["cx"].toDouble(), o["cy"].toDouble());
            QPointF from, to;
            if (axis == 3) {
                from = center;
                to = center + QPointF(35, 0);
            } else if (axis == 5) {
                const auto plane = o["planes"].toList()[1].toMap()["points"].toList();
                for (auto p : plane)
                    from += QPointF(p.toMap()["x"].toDouble(), p.toMap()["y"].toDouble()) / 4;
                to = from + (direction(o, 0) + direction(o, 2)) * amount;
            } else {
                auto d = direction(o, axis);
                from = center + d * .8;
                to = from + d * amount;
            }
            const auto a = viewport->mapToScene(from).toPoint(), b = viewport->mapToScene(to).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
            check(viewport->gizmoDragging() && viewport->gizmoOverlay()["axis"].toInt() == axis,
                  "creation handle hit test uses shared geometry");
            QTest::mouseMove(window, b, 40);
            check(!viewport->gizmoOverlay()["readout"].toString().isEmpty(),
                  "creation drag reports actual change");
            const auto beforeRelease = viewport->gizmoOverlay();
            const auto target = editor.renderCreation();
            bool steady = true;
            auto checkPosition = [&] {
                const auto current = viewport->gizmoOverlay();
                steady &= current["active"].toBool() && editor.renderCreation() == target;
                for (auto key : {"cx", "cy"})
                    steady &= std::abs(current[key].toDouble() - beforeRelease[key].toDouble()) < 1.;
            };
            const auto connection = QObject::connect(&editor, &Editor::placementChanged, &app, checkPosition);
            if (cancel)
                QTest::keyClick(window, Qt::Key_Escape);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b);
            QObject::disconnect(connection);
            if (!cancel && mode == 0) {
                checkPosition();
                check(steady && editor.world().creation == target,
                      "released translation stays at the preview position through publication");
            }
        };
        editor.setSelectedNode(volume.id);
        const auto baseline = editor.world().creation;
        const auto cachedGeneration = editor.world().generated;
        editor.beginCreationTransform();
        QElapsedTimer previewTimer;
        previewTimer.start();
        for (int step = 1; step <= 120; ++step)
            editor.previewCreationTransform({{"x", step * .1}});
        std::cout << "PCG 120 transform previews: " << previewTimer.elapsed() << " ms" << std::endl;
        check(previewTimer.elapsed() < 1500, "PCG gesture avoids synchronous generation on pointer updates");
        check(editor.world().generated == cachedGeneration, "drag retains cached generated geometry");
        editor.endCreationTransform(true);
        check(editor.world().creation == baseline, "preview cancel preserves original authority");
        gesture(0, 0, false);
        check(editor.world().creation.volumes.front().min.x() != baseline.volumes.front().min.x(),
              "PCG arrow translates bounds");
        write("undo", {});
        check(editor.world().creation == baseline, "PCG gesture is one undo");
        gesture(0, 3, true);
        check(editor.world().creation == baseline, "PCG center Esc restores bounds and generated data");
        gesture(2, 5, false, .25);
        auto size = editor.world().creation.volumes.front().max - editor.world().creation.volumes.front().min;
        check(size.x() > 96 && std::abs(size.x() - size.z()) < .01 && std::abs(size.y() - 60) < .01,
              "PCG plane scale is proportional and preserves other axis");
        capture("08-pcg-unified-gizmo");
        write("undo", {});
        viewport->setGizmoMode(1);
        check(viewport->gizmoOverlay()["active"].toBool() &&
                  viewport->gizmoOverlay()["axes"].toList().size() == 3,
              "PCG volume exposes rotation on three axes");
        editor.setSelectedNode(placedId);
        viewport->setGizmoMode(0);
        call("camera", {{"position", QJsonArray{52.5, 28.5, 19}}, {"yaw", 0}, {"pitch", 0}});
        settle();
        editor.setSelectedNode({});
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          viewport->mapToScene({viewport->width() / 2, viewport->height() / 2}).toPoint());
        check(editor.selectedNode() == placedId, "clicking tree trunk selects the placed structure");
        call("camera", {{"position", QJsonArray{105, 100, 140}}, {"yaw", -36}, {"pitch", -27}});
        const auto worldX = direction(viewport->gizmoOverlay(), 0);
        viewport->setGizmoMode(2);
        const auto localZ = direction(viewport->gizmoOverlay(), 2);
        check(QPointF::dotProduct(worldX, localZ) < 0,
              "structure local scale follows clockwise quarter-turn yaw");
        gesture(2, 2, false);
        check(editor.world().creation.presets.front().scale.z() == 2 &&
                  editor.world().creation.presets.front().scale.x() == 1,
              "structure local Z handle changes integer Z scale only");
        write("undo", {});
        viewport->setGizmoMode(1);
        const auto rotation = viewport->gizmoOverlay()["axes"].toList();
        check(rotation.size() == 1 && rotation.front().toMap()["axis"].toInt() == 1 &&
                  rotation.front().toMap()["points"].toList().size() == 25,
              "structure exposes Y quarter arc only");
        const auto arc = rotation.front().toMap()["points"].toList();
        const auto arcStart = arc[2].toMap();
        auto arcPoint = viewport->mapToScene({arcStart["x"].toDouble(), arcStart["y"].toDouble()}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, arcPoint);
        check(viewport->gizmoDragging(), "structure rotation arc captures pointer");
        for (int n = 4; n <= 22; n += 2) {
            auto point = arc[n].toMap();
            arcPoint = viewport->mapToScene({point["x"].toDouble(), point["y"].toDouble()}).toPoint();
            QTest::mouseMove(window, arcPoint, 12);
        }
        check(std::abs(editor.world().creation.presets.front().rotation.y() -
                       baseline.presets.front().rotation.y()) == 90,
              "shared rotation arc commits a supported structure quarter turn");
        const int actualYawDelta =
            int(editor.world().creation.presets.front().rotation.y() - baseline.presets.front().rotation.y());
        check(viewport->gizmoOverlay()["readout"].toString().contains(actualYawDelta < 0 ? "-90" : "+90"),
              "structure rotation readout agrees with actual clockwise yaw parameter");
        check(!viewport->gizmoOverlay()["sector"].toList().empty(),
              "structure shows shared rotation protractor");
        check(window->grabWindow().save(out + "/09-asset-unified-gizmo.png"),
              "capture structure rotation protractor");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, arcPoint);
        write("undo", {});
        patch("presets", placedId, {{"scale", QJsonArray{-1, 1, 1}}});
        // The generation dialog returns focus after its close transition.
        QTest::qWait(200);
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "mirror fixture ready for pointer input");
        viewport->forceActiveFocus();
        // The fixture root is x=52 in [-64,63]; move inward, not beyond the world.
        gesture(0, 0, false, -.35);
        check(editor.world().creation.presets.front().scale.x() == -1,
              "moving mirrored structure preserves mirror");
        write("undo", {});
        gesture(2, 0, false);
        check(editor.world().creation.presets.front().scale.x() == -2,
              "scaling mirrored local axis preserves sign");
        write("undo", {});
        write("undo", {});
        editor.setSelectedNode("road-a");
        gesture(0, 0, false);
        check(editor.world().creation.roadNodes.front().position.x() !=
                  baseline.roadNodes.front().position.x(),
              "road arrow moves real node");
        write("undo", {});
        check(editor.world().creation == baseline, "creation gizmo regressions leave scene intact");
        viewport->setGizmoMode(2);
        check(!viewport->gizmoOverlay()["active"].toBool(), "road node does not advertise scaling");
        viewport->setGizmoMode(0);
        write("creation", {{"action", "store_pcg_asset"}, {"id", volume.id}});
        auto project = out + "/creation-demo.wbench";
        check(editor.save(QUrl::fromLocalFile(project)), "save creation project");
        const auto mixedCells = editor.world().generated->cells;
        auto graphCount = editor.world().creation.graphs.size();
        write("creation", {{"action", "split_graph"}, {"id", graph.id}});
        check(editor.world().creation.graphs.size() == graphCount + 1 &&
                  editor.world().generated->cells == mixedCells,
              "split legacy graph preserves all generated voxels");
        write("undo", {});
        auto beforeOpen = editor.world().generated->cells;
        check(editor.open(QUrl::fromLocalFile(project)), "reopen creation project");
        settle();
        check(editor.world().generated->cells == beforeOpen, "save reopen identical authoritative voxels");
        check(editor.world().creation.pcgAssets.size() == 1,
              "PCG library survives project reopen without source file");
        call("camera", {{"position", QJsonArray{100, 400, 140}}, {"yaw", -35}, {"pitch", -60}});
        settle();
        check(editor.scene()->exactArea.isEmpty(), "high camera has no screen-centre precision island");
        capture("07-high-altitude");
        write("set_blocks", {{"blocks", QJsonArray{QJsonObject{{"position", QJsonArray{20, 250, 20}},
                                                               {"block", "minecraft:stone"}}}}});
        settle();
        call("camera", {{"position", QJsonArray{34, 253, 20}}, {"yaw", -90}, {"pitch", -10}});
        settle();
        check(editor.previewContains(20, 20), "approaching an elevated voxel horizontally enables exact detail");
        write("undo", {});
        settle();
        check(editor.scene()->exactArea.isEmpty(), "removing the elevated voxel releases precision at fixed altitude");
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
        check(QMetaObject::invokeMethod(list, "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, 60),
                                        Q_ARG(qreal, contentY + 17)) &&
                  row >= 0,
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
        write("creation", {{"action", "file"}, {"operation", "load_graph"}, {"path", portableGraph}});
        check(editor.world().creation.assets.size() == 1 && editor.world().creation.graphs.size() == 1,
              "repeat external graph import is idempotent");
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
            if (!editor.world().heights.instances.empty()) {
                const auto terrainId = editor.world().heights.instances.front().id;
                openCard(terrainId);
                auto *terrainView = window->findChild<Viewport *>("terrainAssetViewport");
                check(terrainView && QTest::qWaitFor(
                                         [&] {
                                             return !inspector->meshBusy() &&
                                                    inspector->scene()->vertices > 0 &&
                                                    inspector->world().heights.instances.size() == 1;
                                         },
                                         30000),
                      "legacy heightmap renders independent relief");
                check(inspector->scene()->vertices <= 129 * 129, "heightmap inspector sampling budget");
                QTest::qWait(250);
                check(window->grabWindow().save(out + "/BH-heightmap.png"), "capture terrain editor");
                scene();
            }
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
        check(window->property("inScene").toBool() && window->property("openAssetCount").toInt() == 0,
              "project replacement clears prior asset tabs");
        write("creation", {{"action", "new_graph"}, {"name", "苔石地表"}, {"surface_material", true}});
        const QString material = editor.world().creation.graphs.back().id;
        openCard(material);
        check(panel->property("kind") == "material" && window->findChild<QQuickItem *>("pcgGraphEditor"),
              "new surface material is a directly editable library asset");
        write("undo", {});
        check(window->property("inScene").toBool() && window->property("openAssetCount").toInt() == 0,
              "undoing creation closes a deleted asset tab");
        write("redo", {});
        openCard(material);
        QMetaObject::invokeMethod(window, "closeTab", Q_ARG(QVariant, material));
        if (!editor.world().heights.instances.empty()) {
            const auto terrain = editor.world().heights.instances.front().id;
            openCard(terrain);
            check(panel->property("kind") == "terrain" && editor.selectedNode() == terrain &&
                      editor.terrainDetails()["id"] == terrain,
                  "terrain tab targets the chosen heightmap");
            QMetaObject::invokeMethod(window, "closeTab", Q_ARG(QVariant, terrain));
        }
        shelf->setProperty("expanded", false);
        editor.setTool(0);
        QTest::qWait(150);
        check(viewport->width() >= 400 && viewport->height() >= 250,
              "minimum window leaves a usable scene viewport with brush and inspector");
        check(window->grabWindow().save(out + "/10-compact-scene.png"), "compact scene capture");
        int page = 0;
        for (const auto &id : {editor.world().creation.graphs.front().id,
                               editor.world().creation.assets.front().id, QString("@road")}) {
            ++page;
            QMetaObject::invokeMethod(window, "openAsset", Q_ARG(QVariant, id));
            QTest::qWait(100);
            auto *creationItem = qobject_cast<QQuickItem *>(panel);
            check(creationItem->width() >= 1000 && creationItem->height() >= 450,
                  "creation tabs use full available workspace at minimum window size");
            check(window->grabWindow().save(out + "/10-compact-" + QString::number(page) + ".png"),
                  "compact creation workspace capture");
        }
        scene();
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
