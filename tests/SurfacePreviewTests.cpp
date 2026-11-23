#include "Editor.h"
#include "Viewport.h"
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
    app.setApplicationName("Worldbench precision preview tests");
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
        const QString out = args.value(1, temporaryOutput.path()), source = args.value(2);
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
        if (!source.isEmpty()) {
            check(QFile::copy(source, out + "/source-copy.wbench"), "copy source before opening");
            check(editor.open(QUrl::fromLocalFile(out + "/source-copy.wbench")), "open copied project");
        } else
            check(editor.createWorld(512, 42, 24, 0, -60, true, 384),
                  "create deterministic rectangular fixture");
        settle();
        std::cout << "Loaded " << editor.world().width << "x" << editor.world().depth
                  << "; instances=" << editor.world().heights.instances.size()
                  << " overrides=" << editor.world().overrides.size() << std::endl;
        QStringList images = args.mid(3);
        const bool benchmarkLighting = images.removeAll("--lighting") > 0;
        if (images.empty()) {
            QImage image(65, 65, QImage::Format_Grayscale16);
            for (int z = 0; z < 65; ++z)
                for (int x = 0; x < 65; ++x)
                    reinterpret_cast<quint16 *>(image.scanLine(z))[x] = quint16(
                        65535 * std::exp(-std::pow((x - 22 - .3 * z) / 8, 2) - std::pow((z - 32) / 22., 2)));
            check(image.save(out + "/ridge.png"), "write generated fixture");
            images << out + "/ridge.png";
        }
        std::vector<QString> imported;
        for (const auto &path : images) {
            const auto before = editor.world().heights.instances.size();
            const auto camera = viewport->camera();
            editor.importHeightmap(QUrl::fromLocalFile(path));
            settle();
            check(editor.world().heights.instances.size() == before + 1,
                  "import through normal asynchronous importer");
            check(viewport->camera() == camera, "import preserves camera");
            imported.push_back(editor.world().heights.instances.back().id);
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
                      15000),
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
        const int extent = std::min(editor.world().width, editor.world().depth);
        const int width = std::min(1536, extent / 2);
        for (size_t i = 0; i < imported.size(); ++i) {
            const int x = editor.world().originX + editor.world().width / 2 - width + int(i % 2) * width;
            const int z = editor.world().originZ + editor.world().depth / 2 - width + int(i / 2) * width;
            write("terrain_instance", {{"id", imported[i]},
                                       {"changes", QJsonObject{{"x", x},
                                                               {"z", z},
                                                               {"width", width},
                                                               {"depth", width},
                                                               {"low", 24},
                                                               {"high", 250},
                                                               {"blend", 4},
                                                               {"blendEdge", 32},
                                                               {"rotationY", int(i) * 17}}}});
            settle();
        }
        write("environment",
              {{"changes", QJsonObject{{"enabled", benchmarkLighting}, {"atmosphere", false}}}});
        const auto authoritative = editor.world().heights;
        check(editor.save(QUrl::fromLocalFile(out + "/terrain-test.wbench")), "save only prepared test copy");
        const double height = std::max(400., extent * .75);
        call("camera", {{"position", QJsonArray{0, height, extent * .85}}, {"yaw", 0}, {"pitch", -41}});
        settle();
        auto capture = [&](const QString &name) {
            settle();
            auto content = call("screenshot", {})["content"].toArray();
            check(!content.empty() && content[0].toObject()["type"] == "image", "MCP returns PNG image");
            auto data = QByteArray::fromBase64(content[0].toObject()["data"].toString().toLatin1());
            QImage image;
            check(image.loadFromData(data, "PNG"), "MCP PNG decodes");
            check(image.save(out + "/" + name + ".png"), "save MCP screenshot evidence");
        };
        capture("default-overview");
        QElapsedTimer build;
        build.start();
        settle();
        const auto initialMs = build.elapsed();
        check(editor.world().heights == authoritative && !editor.modified(),
              "preview never bakes or modifies project");
        check(!editor.scene()->farTiles.empty() && editor.scene()->chunks.empty() &&
                  editor.scene()->surfaceError <= 1,
              "high-altitude overview uses accurate surface without an exact voxel island");
        capture("surface-overview");
        auto initial = info();
        const auto beforeNavigation = editor.scene();
        call("camera", {{"position", QJsonArray{0, 350, width * 1.15}}, {"yaw", 0}, {"pitch", -14}});
        capture("surface-ridges");
        size_t navigationReuse = 0;
        for (size_t i = 0; i < beforeNavigation->farTiles.size(); ++i)
            navigationReuse += beforeNavigation->farTiles[i].mesh == editor.scene()->farTiles[i].mesh;
        check(navigationReuse > 0, "navigation reuses terrain away from the moving exact cutout");
        const auto target = editor.world().heights.instances.back();
        editor.setSelectedNode(target.id);
        QElapsedTimer transformClock;
        QJsonArray transformPublications;
        QJsonArray transformInputTimes;
        QJsonArray transformFrameTimes;
        QJsonArray transformDisplayTimes;
        qint64 lastFrameNs = 0;
        uint64_t lastDisplayed = editor.scene()->sourceRevision;
        bool measuringTransform = false;
        const auto transformConnection = QObject::connect(&editor, &Editor::sceneChanged, &app, [&] {
            if (measuringTransform && editor.scene()->provisional)
                transformPublications.append(
                    QJsonObject{{"ms", double(transformClock.elapsed())},
                                {"source_revision", double(editor.scene()->sourceRevision)},
                                {"current_revision", double(editor.revision())},
                                {"build_ms", double(editor.scene()->buildMilliseconds)},
                                {"samples", double(editor.scene()->updatedSamples)},
                                {"tiles", editor.scene()->updatedFarTiles}});
        });
        const auto initialFrames = viewport->frameCounter()->load();
        transformClock.start();
        measuringTransform = true;
        const auto frameConnection = QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
            // A queued frameSwapped callback can still run after disconnect.
            // Exclude screenshot/MCP/readback and post-release frames.
            if (!measuringTransform)
                return;
            const auto now = transformClock.nsecsElapsed();
            if (lastFrameNs)
                transformFrameTimes.append((now - lastFrameNs) / 1000000.0);
            lastFrameNs = now;
            const auto current = viewport->renderMetrics()->renderedSourceRevision.load();
            if (current != lastDisplayed) {
                transformDisplayTimes.append(now / 1000000.0);
                lastDisplayed = current;
            }
        });
        editor.beginTerrainEdit();
        for (int n = 0; n < 240; ++n) {
            QElapsedTimer inputTime;
            inputTime.start();
            editor.setTerrainOption("x", target.x + (n % 96) + 1);
            transformInputTimes.append(inputTime.nsecsElapsed() / 1000000.0);
            QTest::qWait(16);
        }
        const auto transformMs = transformClock.elapsed();
        measuringTransform = false;
        QObject::disconnect(frameConnection);
        const auto transformFrames = viewport->frameCounter()->load() - initialFrames;
        const auto transformCount = transformPublications.size();
        check(transformPublications.size() >= 2,
              "large instance publishes multiple coherent previews while pointer keeps moving");
        check(window->grabWindow().save(out + "/transform-active.png"),
              "capture unreleased instance preview");
        const auto activePreviewInfo = info()["preview"].toObject();
        check(activePreviewInfo["provisional"].toBool() && activePreviewInfo["surface_error_bound"].isNull(),
              "MCP explicitly reports temporary geometry without an uncertified error bound");
        QElapsedTimer releaseClock;
        releaseClock.start();
        editor.endTerrainEdit();
        settle();
        const auto convergenceMs = releaseClock.elapsed();
        QObject::disconnect(transformConnection);
        check(!editor.scene()->provisional && editor.scene()->sourceRevision == editor.revision() &&
                  editor.scene()->surfaceError <= 1,
              "released instance converges to exact revision at the current camera distance");
        for (const auto &tile : editor.scene()->farTiles)
            check(!tile.mesh->provisional, "all low-detail transform tiles replaced");
        capture("transform-final");
        write("undo", {});
        settle();
        check(editor.world().heights == authoritative, "live transform is one undo transaction");
        std::cout << "Live transform snapshots: " << transformPublications.size() << std::endl;
        // Exercise cancellation while a snapshot is in flight, followed by a
        // gesture that returns to its initial value. Neither may strand a
        // provisional tile or let an old gesture overwrite the restored world.
        editor.beginTerrainEdit();
        editor.setTerrainOption("x", target.x + 80);
        QTest::qWait(20);
        editor.endTerrainEdit(true);
        settle();
        check(editor.world().heights == authoritative && !editor.scene()->provisional &&
                  editor.scene()->sourceRevision == editor.revision(),
              "cancel rejects in-flight preview");
        editor.beginTerrainEdit();
        editor.setTerrainOption("x", target.x + 40);
        check(QTest::qWaitFor([&] { return editor.scene()->provisional; }, 30000),
              "return-to-start fixture publishes a preview");
        editor.setTerrainOption("x", target.x);
        editor.endTerrainEdit();
        settle();
        check(editor.world().heights == authoritative && !editor.modified() && !editor.scene()->provisional &&
                  editor.scene()->sourceRevision == editor.revision(),
              "return-to-start restores final precision without undo entry");
        const int x = std::clamp(target.x + target.width / 2, editor.world().originX,
                                 editor.world().originX + editor.world().width - 1);
        const int z = std::clamp(target.z + target.depth / 2, editor.world().originZ,
                                 editor.world().originZ + editor.world().depth - 1);
        const int h = editor.world().height(x, z);
        call("camera", {{"position", QJsonArray{x + 160, h + 170, z + 250}}, {"yaw", -32}, {"pitch", -28}});
        capture("surface-detail");
        const auto beforeEdit = editor.scene();
        write("sculpt", {{"position", QJsonArray{x, h, z}}, {"tool", 0}, {"radius", 8}, {"strength", 3}});
        settle();
        check(editor.world().height(x, z) > h, "MCP sculpt changes authoritative terrain");
        check(editor.scene()->sourceRevision == editor.revision(), "current revision visible after sculpt");
        auto edited = info();
        check(editor.scene()->updatedFarTiles <= 4 && editor.scene()->updatedSamples <= 4 * 129 * 129,
              "local stroke rebuilds at most four small patches");
        size_t reused = 0;
        for (size_t i = 0; i < beforeEdit->farTiles.size(); ++i)
            reused += beforeEdit->farTiles[i].mesh == editor.scene()->farTiles[i].mesh;
        check(reused > 0, "untouched terrain and color pages retain their GPU cache identity");
        capture("surface-edited");
        write("undo", {});
        settle();
        check(editor.world().heights == authoritative, "MCP undo restores exact source");
        write("redo", {});
        settle();
        check(editor.world().height(x, z) > h, "MCP redo restores edit");
        write("undo", {});
        settle();
        write("terrain_instance", {{"id", target.id}, {"changes", QJsonObject{{"enabled", false}}}});
        settle();
        capture("surface-hidden");
        write("undo", {});
        settle();
        check(editor.world().heights == authoritative, "visibility undo restores source");
        write("terrain_instance", {{"id", target.id}, {"changes", QJsonObject{{"rotationY", 35}}}});
        settle();
        check(editor.scene()->updatedFarTiles < int(editor.scene()->farTiles.size()),
              "instance transform invalidates its old and new footprint, not the entire map");
        const auto transformed = info();
        capture("surface-rotated");
        write("undo", {});
        settle();
        call("camera",
             {{"position", QJsonArray{0, editor.world().height(0, 0) + 25, 0}}, {"yaw", 20}, {"pitch", -25}});
        settle();
        check(editor.scene()->chunks.size() > 0 && !editor.scene()->exactArea.isEmpty(),
              "exact voxel inspection is part of the default view without a mode switch");
        capture("voxel-inspection");
        write("environment", {{"changes", QJsonObject{{"enabled", true},
                                                      {"sun", true},
                                                      {"sky", true},
                                                      {"atmosphere", false},
                                                      {"azimuth", -35},
                                                      {"elevation", 28}}}});
        capture("surface-lighting");
        write("undo", {});
        settle();
        check(editor.world().heights == authoritative, "MCP changes all undo cleanly");
        write("sculpt", {{"position", QJsonArray{x, h, z}},
                         {"tool", 4},
                         {"radius", 8},
                         {"strength", 1},
                         {"material_id", int(wb::Stone)}});
        capture("surface-material");
        check(editor.world().block(x, h, z) == wb::Stone, "MCP material edit updates source surface block");
        write("undo", {});
        settle();

        // Hold one GUI brush gesture open while stamps keep arriving. This is
        // deliberately separate from the real stdio tests: GUI stroke lifetime
        // and intermediate publication must also be checked before mouse-up.
        editor.setTool(0);
        editor.setRadius(8);
        editor.setStrength(1);
        QElapsedTimer streamClock;
        QJsonArray publications;
        uint64_t publishedRevision = editor.scene()->sourceRevision;
        bool ordered = true;
        const auto connection = QObject::connect(&editor, &Editor::sceneChanged, &app, [&] {
            ordered = ordered && editor.scene()->sourceRevision >= publishedRevision;
            publishedRevision = editor.scene()->sourceRevision;
            publications.append(QJsonObject{{"ms", double(streamClock.elapsed())},
                                            {"source_revision", double(publishedRevision)},
                                            {"current_revision", double(editor.revision())},
                                            {"build_ms", double(editor.scene()->buildMilliseconds)},
                                            {"updated_tiles", editor.scene()->updatedFarTiles}});
        });
        streamClock.start();
        editor.beginStroke();
        for (int i = 0; i < 30; ++i) {
            const int currentHeight = editor.world().height(x, z);
            wb::Hit hit{true,
                        x,
                        currentHeight,
                        z,
                        {float(x), float(currentHeight), float(z)},
                        {0, 1, 0},
                        editor.world().block(x, currentHeight, z)};
            editor.stamp(hit, false);
            QTest::qWait(15);
        }
        check(!publications.empty(), "continuous gesture publishes visible updates before mouse-up");
        check(window->grabWindow().save(out + "/editing-active.png"),
              "capture actual UI during open brush gesture");
        auto *statusItem = qobject_cast<QQuickItem *>(window->findChild<QObject *>("terrainUpdateStatus"));
        check(statusItem && statusItem->isVisible() &&
                  statusItem->mapToItem(viewport, {0, 0}).y() > viewport->height() * .5,
              "background build feedback stays at bottom during sculpting");
        check(!editor.property("surfacePreview").isValid(), "retired prototype switch is not exposed");
        for (auto *item : window->findChildren<QQuickItem *>())
            check(!item->property("text").toString().contains("正在构建初始体素网格"),
                  "no centered initial-build popup is present while sculpting");
        editor.endStroke();
        settle();
        QObject::disconnect(connection);
        check(ordered && editor.scene()->sourceRevision == editor.revision(),
              "intermediate snapshots advance monotonically and finish at current revision");
        for (const auto &tile : editor.scene()->farTiles)
            for (size_t vi = 0; vi < tile.mesh->referenceVertices; ++vi) {
                const auto &vertex = tile.mesh->opaque[vi];
                const int wx = std::clamp(int(std::floor(vertex.x)), editor.world().originX,
                                          editor.world().originX + editor.world().width - 1);
                const int wz = std::clamp(int(std::floor(vertex.z)), editor.world().originZ,
                                          editor.world().originZ + editor.world().depth - 1);
                check(vertex.y == editor.world().height(wx, wz) + 1,
                      "final cached vertices match the current authoritative columns");
            }
        write("undo", {});
        settle();
        check(editor.world().heights == authoritative, "continuous gesture is exactly one undo transaction");
        editor.setTool(-1);
        if (!source.isEmpty())
            check(digest(source) == sourceHash, "user original bytes unchanged");
        QJsonObject report{{"checks", checks},
                           {"initial_wall_ms", double(initialMs)},
                           {"initial", initial},
                           {"after_local_edit", edited},
                           {"after_transform", transformed},
                           {"continuous_publications", publications},
                           {"transform_publications", transformPublications},
                           {"transform_input_ms", transformInputTimes},
                           {"transform_frame_ms", transformFrameTimes},
                           {"transform_display_ms", transformDisplayTimes},
                           {"benchmark_lighting", benchmarkLighting},
                           {"transform_wall_ms", double(transformMs)},
                           {"transform_frames", transformFrames},
                           {"transform_fps", 1000.0 * transformFrames / transformMs},
                           {"transform_mesh_hz", 1000.0 * transformCount / transformMs},
                           {"transform_convergence_ms", double(convergenceMs)},
                           {"source_sha256", QString(sourceHash.toHex())},
                           {"transcript", transcript},
                           {"scope", "Default precision terrain and local voxel integration; complex 8K/16K "
                                     "and distant non-heightfield geometry remain pending"}};
        QFile file(out + "/report.json");
        check(file.open(QIODevice::WriteOnly), "write evidence report");
        file.write(QJsonDocument(report).toJson());
        file.close();
        editor.setMcpEnabled(false);
        bridge.closeWriteChannel();
        if (!bridge.waitForFinished(5000)) {
            bridge.kill();
            bridge.waitForFinished();
        }
        std::cout << "Surface preview checks: " << checks << " passed; output=" << out.toStdString()
                  << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
