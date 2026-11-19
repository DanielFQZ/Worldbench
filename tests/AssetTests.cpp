#include "core/Assets.h"
#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/Project.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <private/qzipwriter_p.h>
#include <stdexcept>

static int checks = 0;
static void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    try {
        QImage image(16, 32, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        image.setPixelColor(0, 0, Qt::transparent);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const auto base = tmp.filePath("base.jar");
        {
            QZipWriter z(base);
            z.addFile("version.json", R"({"id":"1.20.1"})");
            z.addFile("assets/test/textures/block/stone.png", png);
            z.addFile(
                "assets/test/models/block/base.json",
                R"({"textures":{"a":"test:block/stone"},"elements":[{"from":[4,0,4],"to":[12,16,12],"faces":{"up":{"texture":"#a"},"north":{"texture":"#a"},"south":{"texture":"#a"},"east":{"texture":"#a"},"west":{"texture":"#a"},"down":{"texture":"#a"}}}]})");
            z.addFile("assets/test/models/block/inherited.json", R"({"parent":"test:block/base"})");
            z.addFile(
                "assets/test/blockstates/pillar.json",
                R"({"variants":{"axis=y":{"model":"test:block/inherited"},"axis=x":{"model":"test:block/inherited","x":90}}})");
            z.addFile(
                "assets/test/blockstates/fence.json",
                R"({"multipart":[{"apply":{"model":"test:block/base"}},{"when":{"north":"true"},"apply":{"model":"test:block/base","y":90,"uvlock":true}}]})");
            z.close();
        }
        QString error;
        auto lib = wb::AssetLibrary::load({base}, error);
        check(bool(lib), qPrintable(error));
        check(lib->version == "1.20.1" && lib->catalog.size() == 2, "catalog/version");
        auto state = lib->catalog["test:pillar"].defaults;
        check(state.properties["axis"] == "y", "default axis");
        auto model = lib->model(state);
        check(model->faces.size() == 6 && !model->full && !model->fallback, "inherited partial model");
        check(lib->thumbnails[model->thumbnail].pixelColor(10, 10) == QColor(Qt::red),
              "first frame thumbnail");
        bool transparent = false;
        for (int y = 0; y < lib->atlas.height(); ++y)
            for (int x = 0; x < lib->atlas.width(); ++x)
                transparent |= lib->atlas.pixelColor(x, y).alpha() == 0;
        check(transparent, "atlas preserves cutout alpha instead of blending against white");
        auto fence = lib->catalog["test:fence"].defaults;
        check(fence.properties["north"] == "false" && lib->model(fence)->faces.size() == 6,
              "multipart absent connection default");
        fence.properties["north"] = "true";
        check(lib->model(fence)->faces.size() == 12, "multipart added connection");
        auto w = wb::World::create(32);
        w.setAssets(lib);
        auto id = w.intern(state);
        w.setBlock(0, 80, 0, id);
        auto hit = w.raycast({.5f, 90, .5f}, {0, -1, 0});
        check(hit.valid && hit.y == 80 && hit.block == id, "model triangle picking");
        hit = w.raycast({.1f, 81, .1f}, {0, -1, 0});
        check(hit.y != 80, "ray misses empty model space");
        const auto project = tmp.filePath("models.wbench");
        check(wb::saveProject(project, w, error), "v2 save");
        wb::World loaded;
        check(wb::loadProject(project, loaded, error) && loaded.palette == w.palette &&
                  loaded.overrides == w.overrides && loaded.resourcePaths == w.resourcePaths,
              "v2 dynamic roundtrip");
        const auto overlay = tmp.filePath("overlay.zip");
        image.fill(Qt::blue);
        png.clear();
        buffer.close();
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        {
            QZipWriter z(overlay);
            z.addFile("assets/test/textures/block/stone.png", png);
            z.close();
        }
        auto replaced = wb::AssetLibrary::load({base, overlay}, error);
        check(replaced && replaced->thumbnails["test:block/stone"].pixelColor(0, 0) == QColor(Qt::blue),
              "later pack wins");
        const auto bad = tmp.filePath("bad.zip");
        {
            QZipWriter z(bad);
            z.addFile("assets/test/../escape.json", "{}");
            z.close();
        }
        check(!wb::AssetLibrary::load({bad}, error), "unsafe resource path rejected");
        {
            QZipWriter z(bad);
            z.addFile("assets/test/blockstates/bad.json", "{invalid");
            z.close();
        }
        check(!wb::AssetLibrary::load({bad}, error), "invalid JSON rejected");
        if (app.arguments().size() > 1) {
            auto real = wb::AssetLibrary::load({app.arguments()[1]}, error);
            check(bool(real), qPrintable(error));
            check(real->version == "1.20.1" && real->catalog.size() > 1000,
                  "official client complete asset catalog");
            for (const auto &name :
                 QStringList{"anvil", "oak_door", "oak_fence", "lantern", "oak_slab", "oak_stairs"}) {
                auto s = real->catalog["minecraft:" + name].defaults;
                auto m = real->model(s);
                check(!m->fallback && !m->faces.empty() && !m->thumbnail.isEmpty(),
                      qPrintable("real model " + name));
                check(!m->full, qPrintable("noncube " + name));
                for (const auto &f : m->faces)
                    for (int p = 0; p < 4; ++p) {
                        check(std::isfinite(f.points[p].lengthSquared()) && f.uv[p].x() >= 0 &&
                                  f.uv[p].x() <= 1 && f.uv[p].y() >= 0 && f.uv[p].y() <= 1,
                              "model vertices and atlas UV");
                    }
                std::cout << name.toStdString() << ": " << m->faces.size() << " faces\n";
            }
            auto gallery = wb::World::create(32);
            std::fill(gallery.heights.begin(), gallery.heights.end(), 4);
            gallery.overrides.clear();
            gallery.seaLevel = -60;
            gallery.setAssets(real);
            int x = -6;
            for (const auto &name : QStringList{"anvil", "oak_door", "oak_fence", "lantern", "oak_stairs"}) {
                auto s = real->catalog["minecraft:" + name].defaults;
                if (name == "oak_fence") {
                    s.properties["east"] = "true";
                    s.properties["west"] = "true";
                }
                gallery.setBlock(x, 5, 0, gallery.intern(s));
                if (name == "oak_door") {
                    s.properties["half"] = "upper";
                    gallery.setBlock(x, 6, 0, gallery.intern(s));
                }
                x += 3;
            }
            QDir().mkpath("test-output");
            check(wb::saveProject("test-output/model-gallery.wbench", gallery, error), "gallery fixture");
            const QString exportRoot = "test-output/models-" + QUuid::createUuid().toString(QUuid::Id128);
            QDir().mkpath(exportRoot);
            check(wb::exportMinecraft(gallery, exportRoot + "/minecraft").ok,
                  "dynamic blockstate Minecraft export");
            QJsonArray expected;
            for (const auto &[key, b] : gallery.overrides) {
                int x, y, z;
                gallery.decode(key, x, y, z);
                QJsonObject props;
                for (auto p = gallery.palette[b].properties.begin(); p != gallery.palette[b].properties.end();
                     ++p)
                    props[p.key()] = p.value();
                expected << QJsonObject{{"position", QJsonArray{x, y, z}},
                                        {"Name", gallery.palette[b].name},
                                        {"Properties", props}};
            }
            QFile manifest(exportRoot + "/expected_models.json");
            check(manifest.open(QIODevice::WriteOnly), "model export manifest");
            manifest.write(QJsonDocument(expected).toJson());
            manifest.close();
            QFile last("test-output/latest-model-export.txt");
            last.open(QIODevice::WriteOnly);
            last.write(QFileInfo(exportRoot).absoluteFilePath().toUtf8());
            last.close();
            auto unsupported = gallery;
            unsupported.resourceVersion = "1.21";
            check(!wb::exportMinecraft(unsupported, exportRoot + "/wrong-version").ok,
                  "wrong version export rejected");
            unsupported = gallery;
            unsupported.assets.reset();
            check(!wb::exportMinecraft(unsupported, exportRoot + "/missing-assets").ok,
                  "unvalidated dynamic state export rejected");
            int fallback = 0;
            for (auto i = real->catalog.begin(); i != real->catalog.end(); ++i)
                if (real->model(i->defaults)->fallback)
                    ++fallback;
            std::cout << "Client catalog " << real->catalog.size() << ", fallback models " << fallback
                      << ", atlas " << real->atlas.width() << "x" << real->atlas.height() << "\n";
        }
        std::cout << "PASS " << checks << " asset checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
