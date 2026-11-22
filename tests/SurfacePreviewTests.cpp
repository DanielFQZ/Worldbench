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
        check(request("tools/list", {})["tools"].toArray().size() == 15, "MCP discovers current tool schema");
        const auto initialCamera = viewport->camera();
        auto rejected =
            request("tools/call", {{"name", "camera"},
                                   {"arguments", QJsonObject{{"surface_preview", true}, {"pitch", 1000}}}});
        check(rejected["isError"].toBool() && !editor.surfacePreview() && viewport->camera() == initialCamera,
              "invalid combined camera request leaves mode and camera unchanged");
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
        write("environment", {{"changes", QJsonObject{{"enabled", false}, {"atmosphere", false}}}});
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
        capture("legacy-overview");
        QElapsedTimer build;
        build.start();
        call("camera", {{"surface_preview", true}});
        settle();
        const auto initialMs = build.elapsed();
        check(editor.world().heights == authoritative && !editor.modified(),
              "preview never bakes or modifies project");
        check(editor.scene()->surfacePreview && editor.scene()->surfaceError <= 1,
              "adaptive preview active with bounded residual");
        capture("surface-overview");
        auto initial = info();
        call("camera", {{"position", QJsonArray{0, 350, width * 1.15}}, {"yaw", 0}, {"pitch", -14}});
        capture("surface-ridges");
        const auto target = editor.world().heights.instances.back();
        const int x = std::clamp(target.x + target.width / 2, editor.world().originX,
                                 editor.world().originX + editor.world().width - 1);
        const int z = std::clamp(target.z + target.depth / 2, editor.world().originZ,
                                 editor.world().originZ + editor.world().depth - 1);
        const int h = editor.world().height(x, z);
        call("camera", {{"position", QJsonArray{x + 160, h + 170, z + 250}}, {"yaw", -32}, {"pitch", -28}});
        capture("surface-detail");
        write("sculpt", {{"position", QJsonArray{x, h, z}}, {"tool", 0}, {"radius", 8}, {"strength", 3}});
        settle();
        check(editor.world().height(x, z) > h, "MCP sculpt changes authoritative terrain");
        check(editor.scene()->sourceRevision == editor.revision(), "current revision visible after sculpt");
        auto edited = info();
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
        capture("surface-rotated");
        write("undo", {});
        settle();
        call("camera", {{"surface_preview", false}});
        settle();
        capture("voxel-inspection");
        check(!editor.scene()->surfacePreview && editor.scene()->chunks.size() > 0,
              "voxel inspection still renders");
        call("camera", {{"surface_preview", true}});
        settle();
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
        if (!source.isEmpty())
            check(digest(source) == sourceHash, "user original bytes unchanged");
        QJsonObject report{{"checks", checks},
                           {"initial_wall_ms", double(initialMs)},
                           {"initial", initial},
                           {"after_local_edit", edited},
                           {"source_sha256", QString(sourceHash.toHex())},
                           {"transcript", transcript},
                           {"scope", "R0 heightfield prototype, not R1/R2/R3 acceptance"}};
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
