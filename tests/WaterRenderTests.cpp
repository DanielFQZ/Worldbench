#include "Editor.h"
#include "Viewport.h"
#include "core/Project.h"
#include "third_party/tinyexr.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Worldbench water and shadows tests");
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
        check(QTest::qWaitForWindowExposed(&window), "D3D11 exposed");
        window.requestActivate();
        QDir().mkpath("test-output");
        auto settle = [&] {
            check(QTest::qWaitFor([&] { return !editor.meshBusy() && !editor.assetBusy(); }, 60000),
                  "scene settles");
            const int frames = v->frameCounter()->load();
            v->update();
            check(QTest::qWaitFor([&] { return v->frameCounter()->load() > frames; }, 15000), "GPU frame");
            QTest::qWait(80);
        };
        auto capture = [&](const char *name) {
            std::cout << "Capture " << name << std::endl;
            settle();
            const auto im = window.grabWindow();
            check(im.save(QString("test-output/") + name + ".png"), "capture effect evidence");
            return im;
        };
        auto screen = [&](QVector3D p) {
            const auto ndc = v->viewProjection().map(p);
            return QPointF((ndc.x() + 1) * v->width() * .5, (1 - ndc.y()) * v->height() * .5);
        };
        auto mean = [](const QImage &a, const QImage &b, QRect region) {
            region = region.intersected(a.rect());
            double sum = 0;
            int n = 0;
            for (int y = region.top(); y <= region.bottom(); y += 2)
                for (int x = region.left(); x <= region.right(); x += 2) {
                    const auto p = a.pixelColor(x, y), q = b.pixelColor(x, y);
                    sum += std::abs(p.red() - q.red()) + std::abs(p.green() - q.green()) +
                           std::abs(p.blue() - q.blue());
                    n += 3;
                }
            return n ? sum / n : 0.;
        };
        auto world = wb::World::create({512, 42, 12, 0, 0, true, 384});
        for (int z = -32; z <= -4; ++z)
            for (int x = -18; x <= 18; ++x)
                world.heights.set(world.column(x, z), 72);
        // Dry ground receives shadows as well as water.
        for (int z = -85; z <= -38; ++z)
            for (int x = -25; x < 115; ++x)
                world.heights.set(world.column(x, z), 16);
        for (int z = -75; z <= -50; ++z)
            for (int x = -18; x <= 5; ++x)
                world.heights.set(world.column(x, z), 72);
        world.environment.enabled = true;
        world.environment.atmosphere = false;
        world.environment.azimuth = -90;
        world.environment.elevation = 35;
        world.environment.skyIntensity = 40;
        world.environment.waterWaves = false;
        world.environment.waterReflections = false;
        world.environment.shadows = false;
        QString error;
        check(wb::saveProject(temp.filePath("lake.wbench"), world, error), "save lake fixture");
        settle();
        check(editor.open(QUrl::fromLocalFile(temp.filePath("lake.wbench"))), "open lake fixture");
        v->applyCamera({{"position", QJsonArray{100, 95, 170}}, {"yaw", -28}, {"pitch", -24}});
        const auto still = capture("water-still");
        check(editor.scene()->waterVertices > 0, "water in published snapshot");
        // Depth and shoreline controls must affect the rendered water without
        // rebuilding authoritative terrain geometry.
        const auto waterScene = editor.scene();
        editor.setEnvironment({{"waterFogDensity", 100}, {"waterFoamStrength", 100}, {"waterFoamWidth", 12}});
        const auto deepAbsorption = capture("water-depth-foam");
        editor.setEnvironment({{"waterFogDensity", 0}, {"waterFoamStrength", 0}});
        const auto flatAbsorption = capture("water-depth-flat");
        check(mean(deepAbsorption, flatAbsorption, deepAbsorption.rect()) > .15,
              "water depth absorption and shoreline foam change the preview");
        check(editor.scene() == waterScene, "water appearance controls do not rebuild terrain");
        editor.setEnvironment({{"clouds", true}, {"cloudDensity", 75}, {"cloudSpeed", 35}});
        const auto cloudy = capture("sky-clouds");
        editor.setEnvironment({{"clouds", false}});
        const auto clearSky = capture("sky-without-clouds");
        check(mean(cloudy, clearSky, QRect(0, 0, cloudy.width(), cloudy.height() / 3)) > .15,
              "projected sky clouds are visible and toggleable");
        size_t farWater = 0;
        for (const auto &t : editor.scene()->farTiles)
            farWater += t.mesh->water.size();
        check(farWater > 0, "water exists beyond exact region");
        const auto scene = editor.scene();
        editor.setEnvironment({{"water", false}});
        const auto hidden = capture("water-hidden");
        check(mean(still, hidden, still.rect()) > 5, "water visibly covers the lake and distant sea floor");
        check(editor.scene() == scene, "water visibility never rebuilds terrain");
        editor.undo();
        settle();
        check(editor.world().environment.water, "water visibility undo");
        const auto shadowBaseline = capture("water-before-shadows");
        editor.setEnvironment({{"shadows", true}});
        const auto shadowed = capture("water-shadowed");
        const auto wetPoint = screen({65, 13, -15}) * shadowed.devicePixelRatio();
        const auto dryPoint = screen({50, 17, -60}) * shadowed.devicePixelRatio();
        auto around = [](QPointF p) { return QRect(p.toPoint() - QPoint(9, 9), QSize(19, 19)); };
        const auto waterDifference = mean(shadowBaseline, shadowed, around(wetPoint));
        const auto groundDifference = mean(shadowBaseline, shadowed, around(dryPoint));
        std::cout << "Shadow water/ground differences " << waterDifference << " / " << groundDifference
                  << std::endl;
        check(waterDifference > 2 && groundDifference > 5,
              "sun shadows land on both water and terrain at predicted positions");
        editor.setEnvironment({{"waterReflections", true}});
        const auto reflected = capture("water-reflected");
        const auto mirrorPoint = screen({0, 26 - 60, -18}) * reflected.devicePixelRatio();
        const auto reflectedDifference = mean(shadowed, reflected, around(mirrorPoint));
        std::cout << "Reflection difference " << reflectedDifference << std::endl;
        check(reflectedDifference > 1, "scene reflection appears at geometric mirror projection");
        const auto reflectionCount = v->renderMetrics()->reflectionUpdates.load();
        // Changing reflected geometry must change the reflection, not just sky.
        const auto revision = editor.revision();
        QJsonArray blocks;
        for (int z = -32; z <= -4; ++z)
            for (int x = -18; x <= 18; ++x) {
                for (int y = 40; y <= 72; ++y)
                    if (z == -4 || y == 72)
                        blocks.append(QJsonObject{{"position", QJsonArray{x, y, z}},
                                                  {"block", "minecraft:oak_planks"}});
            }
        // Keep each transaction below the existing MCP block limit.
        check(blocks.size() <= 4096, "material fixture fits MCP budget");
        {
            auto result = editor.executeTool("set_blocks",
                                             {{"blocks", blocks}, {"expected_revision", double(revision)}});
            check(!result["isError"].toBool(), "change reflected surface material");
            const auto changed = capture("water-reflection-material");
            check(v->renderMetrics()->reflectionUpdates.load() > reflectionCount &&
                      mean(reflected, changed, reflected.rect()) > .05,
                  "reflection capture refreshes after reflected geometry changes");
            editor.undo();
            settle();
        }
        editor.setEnvironment(
            {{"waterWaves", true}, {"waveStrength", 80}, {"waveDensity", 70}, {"waveSpeed", 65}});
        settle();
        v->forceActiveFocus();
        const auto animatedScene = editor.scene();
        const auto frames = v->frameCounter()->load();
        const auto shadowCount = v->renderMetrics()->shadowUpdates.load(),
                   reflectionAfterEdit = v->renderMetrics()->reflectionUpdates.load();
        const auto first = window.grabWindow();
        QTest::qWait(1500);
        const auto second = window.grabWindow();
        std::cout << "Wave frames " << frames << " -> " << v->frameCounter()->load()
                  << " visible=" << v->renderMetrics()->visibleWater.load() << std::endl;
        check(v->frameCounter()->load() > frames, "water animates while camera is idle");
        check(editor.scene() == animatedScene && !editor.meshBusy(), "waves cause no CPU mesh build");
        check(v->renderMetrics()->shadowUpdates.load() == shadowCount &&
                  v->renderMetrics()->reflectionUpdates.load() == reflectionAfterEdit,
              "idle waves reuse both shadow and reflection captures");
        // The analytic phase is driven by the frame clock; the captured pair is
        // retained for manual visual review because a single compositor frame
        // may quantize subpixel normal changes to identical 8-bit colors.
        check(second.save("test-output/water-waves.png"), "wave capture");
        v->applyCamera({{"position", QJsonArray{100, 14.8, 90}}, {"yaw", 0}, {"pitch", -18}});
        editor.setEnvironment(
            {{"waveDensity", 72}, {"waveSpeed", 0}, {"shadows", false}, {"waterReflections", false}});
        const auto oldMaximum = capture("water-close-old-density");
        editor.setEnvironment({{"waveDensity", 100}});
        const auto fine = capture("water-close-max-density");
        editor.setEnvironment({{"waterWaves", false}});
        const auto flat = capture("water-close-no-waves");
        const auto closeRegion =
            QRect(fine.width() / 4, fine.height() / 2, fine.width() / 2, fine.height() / 3);
        std::cout << "Close waves max/flat " << mean(fine, flat, closeRegion) << ", max/old "
                  << mean(fine, oldMaximum, closeRegion) << std::endl;
        check(mean(fine, flat, closeRegion) > .5 && mean(fine, oldMaximum, closeRegion) > .5,
              "maximum-density fine ripples remain visible at player camera height");
        // Grazing reflected water previously produced long bright interference
        // stripes. Retain multiple distances/densities for actual visual review.
        editor.setEnvironment({{"waterWaves", true}, {"waterReflections", true}, {"shadows", true}});
        v->applyCamera({{"position", QJsonArray{20, 20, 180}}, {"yaw", 0}, {"pitch", -9}});
        capture("water-grazing-max-density");
        editor.setEnvironment({{"waveDensity", 40}});
        capture("water-grazing-medium-density");
        editor.setEnvironment({{"waveDensity", 100}, {"waveSpeed", 65}});
        QTest::qWait(600);
        capture("water-grazing-animated");
        v->applyCamera({{"position", QJsonArray{20, 100, 330}}, {"yaw", 0}, {"pitch", -16}});
        capture("water-distant-max-density");
        editor.setEnvironment({{"waterWaves", false}, {"shadows", false}, {"waterReflections", false}});
        settle();
        const auto sc = v->renderMetrics()->shadowUpdates.load(),
                   rc = v->renderMetrics()->reflectionUpdates.load();
        v->applyCamera({{"position", QJsonArray{90, 90, 160}}, {"yaw", -28}, {"pitch", -24}});
        settle();
        check(sc == v->renderMetrics()->shadowUpdates.load() &&
                  rc == v->renderMetrics()->reflectionUpdates.load(),
              "disabled effects issue no offscreen captures on camera movement");
        auto bands = wb::World::create({128, 42, 20, 0, 0, true, 128});
        const int depths[] = {1, 5, 10, 20};
        for (int z = -64; z < 64; ++z)
            for (int x = -64; x < 64; ++x)
                bands.heights.set(bands.column(x, z), 20 - depths[(x + 64) / 32]);
        bands.environment.enabled = true;
        bands.environment.atmosphere = false;
        bands.environment.waterFogDensity = 100;
        bands.environment.waterFogStart = 5;
        bands.environment.waterFogColor = "#000000";
        bands.environment.waterSurfaceColor = "#000000";
        bands.environment.waterFoamStrength = 0;
        bands.environment.waterWaves = false;
        bands.environment.waterReflections = false;
        check(wb::saveProject(temp.filePath("depth-bands.wbench"), bands, error),
              "save physical depth bands");
        check(editor.open(QUrl::fromLocalFile(temp.filePath("depth-bands.wbench"))), "open depth bands");
        v->applyCamera({{"position", QJsonArray{0, 128, 70}}, {"yaw", 0}, {"pitch", -56}});
        auto depthImage = capture("water-depth-1-5-10-20");
        auto brightness = [&](QVector3D p) {
            QPoint s = (screen(p) * depthImage.devicePixelRatio()).toPoint();
            double value = 0;
            int count = 0;
            for (int y = s.y() - 3; y <= s.y() + 3; ++y)
                for (int x = s.x() - 3; x <= s.x() + 3; ++x) {
                    auto c = depthImage.pixelColor(x, y);
                    value += std::max({c.red(), c.green(), c.blue()});
                    ++count;
                }
            return value / count;
        };
        const double shallow = brightness({-48, 21, 0}), five = brightness({-16, 21, 0}),
                     ten = brightness({16, 21, 0}), twenty = brightness({48, 21, 0});
        std::cout << "Depth band brightness 1/5/10/20: " << shallow << " / " << five << " / " << ten << " / "
                  << twenty << std::endl;
        check(shallow > 20 && five > ten + 10 && ten < 10 && twenty < 5,
              "underwater light path becomes fogged after the start distance while shore remains clear");
        check(editor.setEnvironment({{"waterFogStart", 128}}), "move fog start past all depths");
        auto clearWater = capture("water-fog-start-128");
        check(mean(depthImage, clearWater, QRect(0, 0, clearWater.width(), clearWater.height())) > 2,
              "fog start changes underwater visibility");
        check(editor.setEnvironment(
                  {{"waterFogStart", 0}, {"waterFogDensity", 200}, {"waterFogColor", "#247c69"}}),
              "colored underwater fog");
        auto coloredWater = capture("water-colored-fog");
        check(mean(coloredWater, depthImage, QRect(0, 0, depthImage.width(), depthImage.height())) > 3,
              "water fog color reaches actual submerged terrain");
        v->setShadowQuality(0);
        capture("shadows-economy");
        const int lowBytes = v->renderMetrics()->effectKiB.load();
        v->setShadowQuality(2);
        capture("shadows-high");
        check(v->renderMetrics()->effectKiB.load() > lowBytes,
              "cascade quality changes allocated shadow resolutions");
        const auto originalWidth = v->renderMetrics()->pixelWidth.load();
        v->setRenderScale(75);
        capture("render-scale-75");
        check(std::abs(v->renderMetrics()->pixelWidth.load() - originalWidth * .75) < 3,
              "render ratio scales 3D physical pixels");
        v->setRenderScale(100);
        settle();
        QFile hdr(temp.filePath("render.hdr"));
        check(hdr.open(QIODevice::WriteOnly), "HDR render fixture");
        hdr.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n");
        hdr.write(QByteArray::fromHex("8040208280402082"));
        hdr.close();
        check(editor.setEnvironment({{"skyTexturePath", hdr.fileName()}, {"clouds", false}}),
              "HDR assigned to environment");
        v->applyCamera({{"position", QJsonArray{0, 128, 70}}, {"yaw", 0}, {"pitch", 30}});
        auto hdrImage = capture("sky-hdr");
        const float linear[] = {.1f, 2.f, .2f, 1.f, .1f, 2.f, .2f, 1.f};
        const char *exrError = nullptr;
        auto exrFile = temp.filePath("render.exr");
        check(SaveEXR(linear, 2, 1, 4, 0, exrFile.toLocal8Bit().constData(), &exrError) == 0,
              "EXR render fixture");
        check(editor.setEnvironment({{"skyTexturePath", exrFile}}), "EXR assigned to environment");
        auto exrImage = capture("sky-exr");
        check(mean(hdrImage, exrImage, QRect(0, 0, exrImage.width(), exrImage.height() / 2)) > 20,
              "HDR and EXR float sky textures reach GPU with distinct colors");
        std::cout << "Water renderer checks " << checks << "; " << v->renderText().toStdString() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << std::endl;
        return 1;
    }
}
