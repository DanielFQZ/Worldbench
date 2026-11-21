#include "Editor.h"
#include "Viewport.h"
#include "core/Project.h"
#include <QBuffer>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLineF>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <private/qzipwriter_p.h>
#include <stdexcept>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Worldbench renderer tests");
    QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    };
    try {
        Editor editor;
        editor.setTool(-1);
        QQuickWindow window;
        window.resize(1280, 800);
        auto *v = new Viewport(window.contentItem());
        v->setSize(QSizeF(1280, 800));
        v->setEditor(&editor);
        v->setRawInput(false);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "renderer window exposed");
        QDir().mkpath("test-output");
        auto settle = [&] {
            check(QTest::qWaitFor([&] { return !editor.meshBusy() && !editor.assetBusy(); }, 60000),
                  "scene settles");
            const int frames = v->frameCounter()->load();
            v->update();
            check(QTest::qWaitFor([&] { return v->frameCounter()->load() > frames; }, 10000),
                  "updated frame renders");
            QTest::qWait(60);
        };
        auto groundVisible = [&](const QString &label) {
            settle();
            const auto img = window.grabWindow();
            check(!img.isNull() && img.save("test-output/" + label + ".png"), "capture rendered frame");
            // Sample the interior of the exact area, never its skirts/far walls.
            // Fixtures have green grass tops and brown dirt risers. Both must
            // be accepted: a sample may legitimately land on a vertical face.
            // Neither matches the blue/gray sky or clear color in these scenes.
            int green = 0, samples = 0;
            for (int y = 48; y <= 58; y += 2)
                for (int x = 44; x <= 56; x += 2) {
                    const auto c = img.pixelColor(img.width() * x / 100, img.height() * y / 100);
                    green += (c.green() > c.red() * 1.2 && c.green() > c.blue() * 1.2) ||
                             (c.red() > c.blue() * 1.2 && c.green() > c.blue() * 1.1);
                    ++samples;
                }
            std::cout << label.toStdString() << " ground samples " << green << "/" << samples << std::endl;
            check(green >= samples * .9, "GPU image must contain near ground, not an empty exact area");
        };
        auto nearCamera = [&] {
            // The 16K fixture has a taller hill here than the user's 8K scene.
            // Stay above the authoritative surface rather than testing from underground.
            const double y = std::max(150, editor.world().height(348, 3392) + 126);
            v->applyCamera({{"position", QJsonArray{348.5, y, 3392.3}}, {"yaw", 0}, {"pitch", -65}});
        };
        check(editor.createWorld(8192, 42, 24, 0, -60, true), "create flat 8K");
        nearCamera();
        groundVisible("render-plain");
        check(editor.setEnvironment({{"enabled", true}}), "enable sky before changing atlas");
        groundVisible("render-lit");
        // Self-contained texture fixture: this catches the D3D11 sky/voxel SRB
        // regression without requiring Minecraft downloads in CTest.
        const auto resource = temp.filePath("grass.zip");
        {
            QImage tile(16, 16, QImage::Format_RGBA8888);
            tile.fill(QColor("#72ac53"));
            QByteArray png;
            QBuffer buffer(&png);
            check(buffer.open(QIODevice::WriteOnly) && tile.save(&buffer, "PNG"), "create test texture");
            QZipWriter zip(resource);
            zip.addFile("assets/minecraft/textures/block/grass_block_top.png", png);
            zip.close();
        }
        editor.importResource(QUrl::fromLocalFile(resource), false);
        groundVisible("render-atlas-switch-lit");
        check(bool(editor.world().assets), "texture resource loaded");
        auto cached = editor.world().assets;
        check(editor.save(QUrl::fromLocalFile(temp.filePath("flat.wbench"))) &&
                  editor.open(QUrl::fromLocalFile(temp.filePath("flat.wbench"))),
              "reopen lit project");
        nearCamera();
        groundVisible("render-cached-reopen");
        check(editor.world().assets == cached, "exercise default-resource reuse");
        const auto args = app.arguments();
        // Optional real-world coverage: <project> [<base.jar> [<overlay.zip>]].
        if (args.size() > 2) {
            editor.importResource(QUrl::fromLocalFile(args[2]), false);
            settle();
        }
        if (args.size() > 1) {
            check(editor.open(QUrl::fromLocalFile(args[1])), "open real render fixture");
            settle();
            if (args.size() > 3) {
                editor.importResource(QUrl::fromLocalFile(args[3]), true);
                settle();
            }
            nearCamera();
            groundVisible("render-real-near");
        }
        for (int i = 0; i < 32; ++i) {
            const double y = std::max(150, editor.world().height(348 + i * 19, 3392 - i * 25) + 126);
            v->applyCamera({{"position", QJsonArray{348.5 + i * 19, y, 3392.3 - i * 25}},
                            {"yaw", 0},
                            {"pitch", -65}});
            QTest::qWait(15);
        }
        groundVisible("render-streamed");
        v->frameWorld();
        settle();
        check(window.grabWindow().save("test-output/render-overview.png"), "capture whole map");
        nearCamera();
        groundVisible("render-return-near");
        check(editor.setEnvironment({{"enabled", false}}), "disable environment");
        groundVisible("render-unlit");
        check(editor.setEnvironment({{"enabled", true}}), "restore environment");
        groundVisible("render-relit");
        editor.removeResource(0);
        groundVisible("render-remove-atlas");
        editor.importResource(QUrl::fromLocalFile(resource), false);
        groundVisible("render-reload-atlas");

        // A stable instance anchor at the map center, with rotated local scale axes.
        auto w = wb::World::create({16384, 42, -60, 24, 0, true});
        wb::TerrainInstance instance;
        instance.id = "gizmo-test";
        instance.name = "Gizmo test";
        instance.sourceName = "test.png";
        instance.width = instance.depth = 64;
        instance.x = instance.z = -32;
        instance.high = 40;
        instance.rotationY = 23;
        auto map = std::make_shared<wb::HeightmapData>();
        map->width = map->depth = 2;
        map->values = {0, 65535, 0, 65535};
        map->buildBounds();
        instance.image = map;
        w.heights.instances.push_back(instance);
        QString error;
        check(wb::saveProject(temp.filePath("gizmo.wbench"), w, error), "save gizmo fixture");
        check(editor.open(QUrl::fromLocalFile(temp.filePath("gizmo.wbench"))), "open gizmo fixture");
        settle();
        editor.setSelectedNode(instance.id);
        auto point = [](QVariant v) {
            const auto p = v.toMap();
            return QPointF(p["x"].toDouble(), p["y"].toDouble());
        };
        for (int height : {800, 500}) {
            window.resize(1280, height);
            v->setHeight(height);
            for (double distance : {50., 1000., 10000., 35000.}) {
                // Look directly at the stable anchor; logical pixel size must
                // remain constant at any viewing distance and viewport height.
                v->applyCamera(
                    {{"position", QJsonArray{0, 47 + distance * .573576436, distance * .819152044}},
                     {"yaw", 0},
                     {"pitch", -35}});
                for (int mode = 0; mode < 3; ++mode) {
                    v->setGizmoMode(mode);
                    const auto overlay = v->gizmoOverlay();
                    check(overlay["active"].toBool(), "gizmo visible from near to full 16K view");
                    const QPointF center(overlay["cx"].toDouble(), overlay["cy"].toDouble());
                    const auto axes = overlay["axes"].toList();
                    double radius = 0;
                    for (const auto &axis : axes)
                        for (const auto &p : axis.toMap()["points"].toList())
                            radius = std::max(radius, QLineF(center, point(p)).length());
                    check(radius > 70 && radius < 120,
                          "gizmo retains usable screen radius without world cap");
                    if (mode == 0) {
                        const auto xPoints = axes[0].toMap()["points"].toList();
                        check(std::abs(QLineF(point(xPoints[0]), point(xPoints[1])).length() - 90) < .1,
                              "screen-parallel axis is 90 logical pixels");
                    }
                    if (distance == 35000) {
                        const auto points = axes[0].toMap()["points"].toList();
                        const auto start =
                            mode == 1 ? point(points[12]) : center + (point(points.last()) - center) * .8;
                        v->forceActiveFocus();
                        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start.toPoint());
                        check(v->gizmoDragging(), "distant translation/rotation/scale handle is pickable");
                        QTest::mouseMove(&window, (start + QPointF(10, -7)).toPoint(), 20);
                        check(!v->gizmoOverlay()["readout"].toString().isEmpty(),
                              "distant drag has feedback");
                        QTest::keyClick(&window, Qt::Key_Escape);
                        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
                                            (start + QPointF(10, -7)).toPoint());
                        check(!v->gizmoDragging() && editor.world().heights.instances.front() == instance,
                              "cancel restores exact transform at long distance");
                    }
                }
            }
        }
        settle();
        std::cout << "Renderer checks " << checks << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << std::endl;
        return 1;
    }
}
