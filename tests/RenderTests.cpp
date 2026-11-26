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
        if (app.arguments().contains("--display-options")) {
            check(editor.createWorld(128, 42, 24, 0, -60, true, 128), "create filter fixture");
            settle();
            QImage tile(32,32,QImage::Format_RGBA8888);
            for(int y=0;y<32;++y)for(int x=0;x<32;++x)
                tile.setPixelColor(x,y,((x+y)%2)?QColor(220,220,220):QColor(30,30,30));
            QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
            check(tile.save(&buffer,"PNG"), "encode frequency fixture");
            const auto pack=temp.filePath("filter.zip");
            QZipWriter zip(pack);
            zip.addFile("assets/minecraft/textures/block/grass_block_top.png",png);zip.close();
            editor.importResource(QUrl::fromLocalFile(pack),false);settle();
            editor.setEnvironment({{"enabled",false},{"atmosphere",false},{"water",false}});
            v->applyCamera({{"position",QJsonArray{.5,27.5,30.5}},{"yaw",0},{"pitch",-12}});
            v->setBlockGrid(false);v->setTextureAnisotropy(1);settle();
            const auto isotropic=window.grabWindow();
            v->setTextureAnisotropy(8);settle();
            const auto anisotropic=window.grabWindow();
            double delta=0;int count=0;
            for(int y=350;y<700;++y)for(int x=200;x<1000;++x){
                delta+=std::abs(qGray(isotropic.pixel(x,y))-qGray(anisotropic.pixel(x,y)));++count;
            }
            check(delta/count>.1, "filtering changes actual grazing-angle GPU pixels");
            check(isotropic.save("test-output/filter-1x.png")&&anisotropic.save("test-output/filter-8x.png"), "capture filter comparison");
            for(bool lit:{false,true}){
                editor.setEnvironment({{"enabled",lit}});
                v->applyCamera({{"position",QJsonArray{.5,38,12.5}},{"yaw",0},{"pitch",-55}});
                v->setBlockGrid(false);settle();const auto off=window.grabWindow();
                v->setBlockGrid(true);settle();const auto on=window.grabWindow();
                int darkened=0,brightened=0;
                for(int y=300;y<700;++y)for(int x=250;x<1000;++x){
                    int change=qGray(off.pixel(x,y))-qGray(on.pixel(x,y));
                    darkened+=change>1;brightened+=change< -1;
                }
                std::cout<<"grid lit="<<lit<<" darkened="<<darkened<<" brightened="<<brightened<<std::endl;
                check(darkened>1000 && brightened<100, "grid only darkens edges in lit and unlit modes");
                check(on.save(QString("test-output/grid-%1-on.png").arg(lit))&&off.save(QString("test-output/grid-%1-off.png").arg(lit)), "capture grid comparison");
                v->setBlockGrid(false);settle();
                const auto restored=window.grabWindow();
                double restoredDelta=0;
                for(int y=300;y<700;++y)for(int x=250;x<1000;++x)
                    restoredDelta+=std::abs(qGray(restored.pixel(x,y))-qGray(off.pixel(x,y)));
                std::cout<<"restored delta="<<restoredDelta/300000<<std::endl;
                check(restoredDelta/300000<.1,"grid disable restores original ground pixels");
            }
            std::cout<<checks<<" display checks passed"<<std::endl;return 0;
        }
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
        if (app.arguments().contains("--resource-regression")) {
            const auto args = app.arguments();
            const int at = args.indexOf("--resource-regression");
            check(args.size() > at + 2, "resource regression requires project and jar");
            editor.importResource(QUrl::fromLocalFile(args[at + 2]), false);
            settle();
            auto camera = [&] {
                const auto &w = editor.world();
                int x = w.originX + w.width / 2, z = w.originZ + w.depth / 2;
                for (int sz = w.originZ + 128; sz < w.originZ + w.depth - 128; sz += 64)
                    for (int sx = w.originX + 128; sx < w.originX + w.width - 128; sx += 64)
                        if (w.height(sx, sz) > w.height(x, z)) { x = sx; z = sz; }
                std::cout << "Regression dry-ground camera " << x << "," << w.height(x,z) + 45 << "," << z << std::endl;
                v->applyCamera({{"position", QJsonArray{x + .5, w.height(x, z) + 45., z + .5}},
                                {"yaw", 0}, {"pitch", -85}});
            };
            check(editor.open(QUrl::fromLocalFile(args[at + 1])), "open old resource project");
            settle(); // Open resets to overview: bounds may render before any exact terrain.
            camera();
            settle();
            const auto before = window.grabWindow();
            before.save("test-output/resource-before-reload.png");
            editor.importResource(QUrl::fromLocalFile(args[at + 2]), false);
            settle();
            const auto after = window.grabWindow();
            after.save("test-output/resource-after-reload.png");
            int different = 0, redBefore = 0, redAfter = 0, samples = 0;
            for (int y = before.height() / 2; y < before.height() * 4 / 5; y += 2)
                for (int x = before.width() * 2 / 5; x < before.width() * 3 / 5; x += 2) {
                    const auto a = before.pixelColor(x, y), b = after.pixelColor(x, y);
                    redBefore += a.red() > 3 * a.green() && a.red() > 3 * a.blue();
                    redAfter += b.red() > 3 * b.green() && b.red() > 3 * b.blue();
                    different += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                                     std::abs(a.blue() - b.blue()) > 40;
                    ++samples;
                }
            std::cout << "Resource regression: changed " << different << "/" << samples
                      << ", red before " << redBefore << ", red after " << redAfter << std::endl;
            check(redBefore <= redAfter + samples / 100, "open must not introduce red terrain before manual reload");
            check(different < samples / 20, "open and manual resource reload must render equivalent terrain");
            check(editor.save(QUrl::fromLocalFile(QDir::current().absoluteFilePath("test-output/resource-roundtrip.wbench"))),
                  "save fixed resource project copy for fresh-process reopen");
            return 0;
        }
        check(editor.createWorld(8192, 42, 24, 0, -60, true), "create flat 8K");
        nearCamera();
        groundVisible("render-plain");
        // Coverage transitions must keep unrelated accurate tiles resident.
        v->applyCamera({{"position", QJsonArray{348.5, 800, 3392.3}}, {"yaw", 0}, {"pitch", -65}});
        settle();
        const auto overviewTiles = editor.scene()->farTiles;
        v->applyCamera({{"position", QJsonArray{348.5, editor.world().height(348, 3392) + 35, 3392.3}},
                        {"yaw", 0},
                        {"pitch", -65}});
        settle();
        size_t retained = 0;
        for (size_t i = 0; i < overviewTiles.size(); ++i)
            retained += overviewTiles[i].mesh == editor.scene()->farTiles[i].mesh;
        check(!editor.scene()->exactArea.isEmpty() && retained + 16 >= overviewTiles.size(),
              "approaching ground restores detail without invalidating the entire map");
        editor.setExactRadiusChunks(3);
        settle();
        check(editor.scene()->updatedFarTiles <= 16, "exact range preference only changes local coverage");
        editor.setExactRadiusChunks(4);
        settle();
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
        check(editor.createPcgGraph("Resource binding regression") && editor.addPcgVolume(),
              "create PCG bounds for overview-first resource regression");
        editor.setSelectedNode("");
        v->frameWorld();
        settle();
        editor.removeResource(0);
        settle();
        editor.importResource(QUrl::fromLocalFile(resource), false);
        settle();
        check(editor.scene()->chunks.empty(), "atlas first used in far overview without exact chunks");
        nearCamera();
        groundVisible("render-bounds-before-exact");
        auto cached = editor.world().assets;
        check(editor.save(QUrl::fromLocalFile(temp.filePath("flat.wbench"))) &&
                  editor.open(QUrl::fromLocalFile(temp.filePath("flat.wbench"))),
              "reopen lit project");
        settle(); // Let the PCG wireframe render in the reset overview before approaching.
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
            v->applyCamera(
                {{"position", QJsonArray{348.5 + i * 19, y, 3392.3 - i * 25}}, {"yaw", 0}, {"pitch", -65}});
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

        check(editor.createWorld(16384, 42, 24, 0, -60, true, 12289), "selection rectangular 16K fixture");
        editor.setTool(6);
        v->frameWorld();
        settle();
        auto screen = [&](float x, float y, float z) {
            const auto p = v->viewProjection() * QVector4D(x, y, z, 1);
            return QPointF((p.x() / p.w() * .5 + .5) * v->width(), (.5 - p.y() / p.w() * .5) * v->height());
        };
        const auto revision = editor.revision();
        // Both endpoints are outside the 9x9 exact patch, with the camera well
        // beyond the former 2500-block pick distance.
        const QPoint first = screen(-3200.5f, 25, -2200.5f).toPoint();
        const QPoint last = screen(5000.5f, 25, 3500.5f).toPoint();
        v->forceActiveFocus();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, first);
        const auto firstHit = v->hit();
        QTest::mouseMove(&window, last, 60);
        // Native mouse movement can be queued by Windows. Wait for delivery
        // before reading the hit; mouseRelease can otherwise flush that move
        // after lastHit was sampled, comparing endpoints from different events.
        check(QTest::qWaitFor([&] {
                  return editor.selectionRect().width() > 8000 && editor.selectionRect().height() > 5500;
              }, 3000), "selection drag movement delivered");
        const auto lastHit = v->hit();
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, last);
        check(firstHit.valid && lastHit.valid && !editor.previewContains(firstHit.x, firstHit.z) &&
                  !editor.previewContains(lastHit.x, lastHit.z),
              "full-map drag hits precise terrain outside resident chunks");
        const auto selected = editor.selectionRect();
        std::cout << "Selection result " << selected.x() << "," << selected.y() << "," << selected.width()
                  << "," << selected.height() << " endpoints " << firstHit.x << "," << firstHit.z << " -> "
                  << lastHit.x << "," << lastHit.z << " revision " << revision << "/" << editor.revision()
                  << " tool " << editor.tool() << std::endl;
        check(selected == QRect(QPoint(firstHit.x, firstHit.z), QPoint(lastHit.x, lastHit.z)).normalized() &&
                  selected.width() > 8000 && selected.height() > 5500 && editor.revision() == revision,
              "whole-domain selection matches authoritative endpoints without editing world");
        settle();
        const auto selectedImage = window.grabWindow();
        check(selectedImage.save("test-output/selection-full-16k.png"), "capture full-map selection");
        int visibleEdges = 0;
        for (int i = 1; i < 20; ++i) {
            const float f = float(i) / 20;
            for (const QPointF p :
                 {screen(float(selected.left()), 25, selected.top() + selected.height() * f),
                  screen(float(selected.right() + 1), 25, selected.top() + selected.height() * f),
                  screen(selected.left() + selected.width() * f, 25, float(selected.top())),
                  screen(selected.left() + selected.width() * f, 25, float(selected.bottom() + 1))}) {
                bool orange = false;
                const auto pixel = p * selectedImage.devicePixelRatio();
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx) {
                        const int x = qRound(pixel.x()) + dx, y = qRound(pixel.y()) + dy;
                        if (!selectedImage.rect().contains(x, y))
                            continue;
                        const auto color = selectedImage.pixelColor(x, y);
                        orange |= color.red() > 170 && color.red() > color.green() * 1.15 &&
                                  color.blue() < color.red() * .65;
                    }
                visibleEdges += orange;
            }
        }
        std::cout << "Full-map orange boundary samples " << visibleEdges << "/76" << std::endl;
        check(visibleEdges >= 68, "far LOD selection boundary is actually visible on all four edges");
        const auto areaBeforeMove = editor.selectionRect();
        v->jumpTo(selected.center().x(), selected.center().y());
        settle();
        check(editor.selectionRect() == areaBeforeMove, "streaming into selection preserves exact bounds");
        check(window.grabWindow().save("test-output/selection-near.png"), "capture selected near terrain");
        editor.clearSelection();
        check(!editor.selectionRect().isValid(), "clear full-map selection");

        v->frameWorld();
        editor.setTool(0);
        editor.setRadius(16);
        editor.setStrength(4);
        settle();
        QTest::mouseMove(&window, screen(5000.5f, 25, 3500.5f).toPoint(), 20);
        const auto brushHit = v->hit();
        check(brushHit.valid && !editor.previewContains(brushHit.x, brushHit.z),
              "brush preview hits unloaded precise surface from full 16K view");
        settle();
        const auto brushImage = window.grabWindow();
        check(brushImage.save("test-output/brush-full-16k.png"), "capture distant brush footprint");
        const auto brushPixel =
            screen(brushHit.x + .5f, 25, brushHit.z + .5f) * brushImage.devicePixelRatio();
        int cyan = 0;
        for (int y = -5; y <= 5; ++y)
            for (int x = -5; x <= 5; ++x) {
                const auto p = brushPixel.toPoint() + QPoint(x, y);
                if (!brushImage.rect().contains(p))
                    continue;
                const auto c = brushImage.pixelColor(p);
                cyan += c.green() > 170 && c.blue() > 170 && c.red() < c.green() * .8;
            }
        check(cyan > 0, "screen-space brush outline remains visible on far LOD");
        for (int tool : {0, 1, 5}) {
            editor.setTool(tool);
            QTest::mouseMove(&window, screen(5000.5f, 25, 3500.5f).toPoint(), 20);
            check(v->hit().valid, "all surface tools can acquire remote authoritative hits");
        }
        editor.setTool(-1);

        // At a practical editing distance the new curve must have a strong,
        // monotonic effect, while 0% is visually the same as disabling atmosphere.
        v->applyCamera({{"position", QJsonArray{0, 175, 200}}, {"yaw", 0}, {"pitch", -35}});
        editor.setEnvironment({{"enabled", true}, {"atmosphere", false}, {"haze", 0}});
        settle();
        const auto noAtmosphere = window.grabWindow();
        editor.setEnvironment({{"atmosphere", true}});
        settle();
        const auto zeroHaze = window.grabWindow();
        const auto sceneBeforeHaze = editor.scene();
        auto difference = [](const QImage &a, const QImage &b) {
            double sum = 0;
            int count = 0;
            for (int y = a.height() * 4 / 10; y < a.height() * 7 / 10; y += 8)
                for (int x = a.width() * 4 / 10; x < a.width() * 6 / 10; x += 8) {
                    const auto ca = a.pixelColor(x, y), cb = b.pixelColor(x, y);
                    sum += std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) +
                           std::abs(ca.blue() - cb.blue());
                    count += 3;
                }
            return sum / count;
        };
        check(difference(noAtmosphere, zeroHaze) < .1, "zero haze has no residual ground fog");
        editor.setEnvironment({{"haze", 50}});
        settle();
        const auto middleHaze = window.grabWindow();
        editor.setEnvironment({{"haze", 100}});
        settle();
        const auto highHaze = window.grabWindow();
        const double middleDifference = difference(middleHaze, zeroHaze),
                     highDifference = difference(highHaze, zeroHaze);
        std::cout << "Atmosphere pixel difference 50/100% " << middleDifference << "/" << highDifference
                  << std::endl;
        check(middleDifference > 1 && highDifference > 20 && highDifference > middleDifference * 3,
              "concentration produces visible monotonic near-distance response");
        check(editor.scene() == sceneBeforeHaze, "atmosphere edits do not rebuild geometry");
        check(zeroHaze.save("test-output/atmosphere-0.png") &&
                  middleHaze.save("test-output/atmosphere-50.png") &&
                  highHaze.save("test-output/atmosphere-100.png"),
              "capture atmosphere comparison");

        // A stable instance anchor at the map center, with rotated local scale axes.
        editor.setTool(-1);
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
        // Real D3D11 verification of per-patch surface color SRBs, both with
        // and without the sky pass and after changing the source resource pack.
        editor.setTool(-1);
        editor.clearSelection();
        check(editor.createWorld(512, 42, 24, 0, -60, true, 384), "create rectangular color fixture");
        settle(); // New-world resource restore must finish before checking colors.
        v->applyCamera({{"position", QJsonArray{0, 500, 1}}, {"yaw", 0}, {"pitch", -89}});
        auto colorPack = [&](const QString &name, QColor color) {
            const auto path = temp.filePath(name + ".zip");
            QImage tile(32, 32, QImage::Format_RGBA8888);
            tile.fill(color);
            QByteArray png;
            QBuffer buffer(&png);
            check(buffer.open(QIODevice::WriteOnly) && tile.save(&buffer, "PNG"),
                  "create surface color fixture");
            QZipWriter zip(path);
            zip.addFile("assets/minecraft/textures/block/grass_block_top.png", png);
            zip.close();
            editor.importResource(QUrl::fromLocalFile(path), false);
            settle();
            const auto image = window.grabWindow();
            check(image.save("test-output/" + name + ".png"), "capture GPU surface color evidence");
            return image;
        };
        editor.setEnvironment({{"enabled", false}, {"atmosphere", false}});
        const auto redSurface = colorPack("surface-pack-red", QColor("#e03020"));
        editor.setEnvironment({{"enabled", true}, {"atmosphere", false}});
        const auto blueSurface = colorPack("surface-pack-blue-lit", QColor("#2040e0"));
        const auto colorPoint = screen(160.5f, 25, 40.5f) * redSurface.devicePixelRatio();
        check(!editor.previewContains(160, 40),
              "color pixel checks actual adaptive surface outside exact chunks");
        const auto red = redSurface.pixelColor(colorPoint.toPoint());
        const auto blue = blueSurface.pixelColor(colorPoint.toPoint());
        check(red.red() > red.blue() * 2 && blue.blue() > blue.red() * 2,
              "GPU surface color follows resource-pack top texture across sky and SRB changes");
        const auto redLitSurface = colorPack("surface-pack-red-lit", QColor("#e03020"));
        const auto redLit = redLitSurface.pixelColor(colorPoint.toPoint());
        check(redLit.red() > redLit.blue() * 2 && difference(redLitSurface, blueSurface) > 20,
              "same lit scene changes color with resources; a blue sky cannot satisfy the color test");
        check(editor.scene()->colorBytes > 0 && !editor.scene()->farTiles.empty(),
              "surface color pages have accounted residency");
        // A cutout must expose edited voxel geometry, not leave the continuous
        // grass plane covering it. Check actual D3D11 pixels over a shallow pit.
        v->applyCamera({{"position", QJsonArray{0, 70, 1}}, {"yaw", 0}, {"pitch", -89}});
        settle();
        const auto flatImage = window.grabWindow();
        QJsonArray holes;
        for (int z = -5; z <= 5; ++z)
            for (int x = -5; x <= 5; ++x)
                for (int y = 23; y <= 24; ++y)
                    holes.append(QJsonObject{{"position", QJsonArray{x, y, z}}, {"block", "minecraft:air"}});
        editor.executeTool("set_blocks",
                           {{"blocks", holes}, {"expected_revision", double(editor.revision())}});
        settle();
        const auto pitImage = window.grabWindow();
        const auto pitPixel = screen(.5f, 23, .5f) * pitImage.devicePixelRatio();
        const auto beforePit = flatImage.pixelColor(pitPixel.toPoint()),
                   afterPit = pitImage.pixelColor(pitPixel.toPoint());
        check(editor.world().block(0, 24, 0) == wb::Air && editor.previewContains(0, 0),
              "pit is in ready exact region");
        check(afterPit.green() > beforePit.green() + 12 && difference(flatImage, pitImage) > 1,
              "actual pit floor is visible; continuous terrain does not cover the opening");
        check(pitImage.save("test-output/default-terrain-pit.png"), "capture default terrain pit evidence");
        editor.undo();
        settle();
        check(difference(flatImage, window.grabWindow()) < 1, "undo restores rendered surface above pit");
        std::cout << "Renderer checks " << checks << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << std::endl;
        return 1;
    }
}
