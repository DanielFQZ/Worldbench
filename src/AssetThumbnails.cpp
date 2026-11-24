#include "Editor.h"
#include <QBuffer>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
QColor fallbackColor(const QString &state) {
    const auto name = state.section('[', 0, 0);
    if (name.contains("leaves") || name.contains("grass"))
        return QColor("#779252");
    if (name.contains("log") || name.contains("planks") || name.contains("dirt"))
        return QColor("#896745");
    if (name.contains("sand"))
        return QColor("#d7c495");
    return QColor("#92999b");
}
// Bake a bounded isometric sprite per block state from the very same model
// geometry, UVs, tint and active atlas as the scene. Overscan fits fences and
// models extending outside their unit cell. Pixel centres avoid edge halos.
QImage blockSprite(const wb::AssetLibrary *lib, const QString &name) {
    constexpr int size = 128;
    constexpr float unit = 36.f;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    wb::BlockState state;
    QString error;
    auto model = lib && wb::parseCreationBlock(name, state, error) ? lib->model(state) : nullptr;
    auto project = [](QVector3D p) {
        return QVector2D(64 + unit * (p.x() - p.z()), 64 + unit * (.45f * (p.x() + p.z()) - p.y()));
    };
    if (!model || !lib || lib->atlas.isNull()) {
        auto cube = wb::fallbackModel(1);
        QPainter painter(&image);
        for (const auto &face : cube->faces) {
            if (QVector3D::dotProduct(face.normal, {1, .9f, 1}) <= 0)
                continue;
            QPolygonF polygon;
            for (const auto &point : face.points)
                polygon << project(point).toPointF();
            painter.setPen(Qt::NoPen);
            painter.setBrush(fallbackColor(name).darker(face.normal.y() > .5f   ? 100
                                                        : face.normal.x() > .5f ? 150
                                                                                : 122));
            painter.drawPolygon(polygon);
        }
        return image;
    }
    const QVector3D eye(1, .9f, 1);
    std::vector<const wb::ModelFace *> faces;
    for (const auto &face : model->faces)
        if (QVector3D::dotProduct(face.normal, eye) > 0)
            faces.push_back(&face);
    std::stable_sort(faces.begin(), faces.end(), [&](const auto *a, const auto *b) {
        return QVector3D::dotProduct(a->points[0] + a->points[2], eye) <
               QVector3D::dotProduct(b->points[0] + b->points[2], eye);
    });
    auto cross = [](QVector2D a, QVector2D b) { return a.x() * b.y() - a.y() * b.x(); };
    const auto &atlas = lib->atlas;
    for (const auto *f : faces) {
        const auto origin = project(f->points[0]);
        const auto dx = project(f->points[1]) - origin, dy = project(f->points[3]) - origin;
        const float det = cross(dx, dy);
        if (std::abs(det) < .0001f)
            continue;
        QPolygonF polygon;
        for (const auto &v : f->points)
            polygon << project(v).toPointF();
        const auto rect = polygon.boundingRect().toAlignedRect().intersected(image.rect());
        const float light = .68f + .28f * std::max(0.f, f->normal.y()) + .12f * std::max(0.f, f->normal.z());
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const QVector2D delta = QVector2D(x + .5f, y + .5f) - origin;
                float u = cross(delta, dy) / det, v = cross(dx, delta) / det;
                if (u < 0 || u >= 1 || v < 0 || v >= 1)
                    continue;
                auto uv = f->uv[0] + u * (f->uv[1] - f->uv[0]) + v * (f->uv[3] - f->uv[0]);
                auto tex = atlas.pixelColor(std::clamp(int(uv.x() * atlas.width()), 0, atlas.width() - 1),
                                            std::clamp(int(uv.y() * atlas.height()), 0, atlas.height() - 1));
                const float alpha = tex.alphaF() * f->color.w();
                if (alpha < .05f)
                    continue;
                auto dest = image.pixelColor(x, y);
                const float behind = dest.alphaF() * (1 - alpha), total = behind + alpha;
                auto channel = [&](float sample, float tint, float previous) {
                    return std::clamp((sample * tint * light * alpha + previous * behind) / total, 0.f, 1.f);
                };
                image.setPixelColor(x, y,
                                    QColor::fromRgbF(channel(tex.redF(), f->color.x(), dest.redF()),
                                                     channel(tex.greenF(), f->color.y(), dest.greenF()),
                                                     channel(tex.blueF(), f->color.z(), dest.blueF()),
                                                     total));
            }
    }
    return image;
}
void drawStructure(QPainter &p, const wb::StructureAsset &asset, const wb::AssetLibrary *lib) {
    const int step = std::max(1, (std::max({asset.width, asset.height, asset.depth}) + 29) / 30);
    const int w = (asset.width + step - 1) / step, h = (asset.height + step - 1) / step,
              d = (asset.depth + step - 1) / step;
    std::vector<QString> cells(w * h * d);
    auto index = [&](int x, int y, int z) { return (y * d + z) * w + x; };
    for (const auto &b : asset.blocks)
        if (!b.state.endsWith(":air") && !b.state.endsWith("_air"))
            cells[index(b.x / step, b.y / step, b.z / step)] = b.state;
    QCache<QString, QImage> sprites(128);
    const double scale = std::min(146. / (w + d), 80. / (.45 * (w + d) + h));
    const auto side = 128 * scale / 36;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int y = 0; y < h; ++y)
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x) {
                const auto &name = cells[index(x, y, z)];
                if (name.isEmpty())
                    continue;
                if (!sprites.contains(name))
                    sprites.insert(name, new QImage(blockSprite(lib, name)));
                double px = 80 + scale * (x - z - (w - d) * .5),
                       py = 45 + scale * (.45 * (x + z) - y - .225 * (w + d) + h * .5);
                p.drawImage(QRectF(px - side * .5, py - side * .5, side, side), *sprites.object(name));
            }
}
void drawTerrain(QPainter &p, const wb::TerrainInstance &terrain) {
    auto mesh = wb::buildTerrainAssetPreview(terrain, 40);
    struct Face {
        float depth;
        QPolygonF polygon;
        QColor color;
    };
    std::vector<Face> faces;
    const QVector3D light = QVector3D(-1, 2, 1).normalized();
    for (size_t i = 0; i < mesh->opaqueIndices.size(); i += 3) {
        Face face{};
        float shade = 0;
        for (int k = 0; k < 3; ++k) {
            const auto &v = mesh->opaque[mesh->opaqueIndices[i + k]];
            face.polygon << QPointF(v.x - v.z, (v.x + v.z) * .42 - v.y);
            face.depth += v.x + v.z + v.y * .84f;
            shade += std::max(0.f, QVector3D::dotProduct(light, {v.nx, v.ny, v.nz}));
        }
        const float lum = .4f + shade * .2f;
        face.color = QColor::fromRgbF(lum * .76f, lum * .84f, lum * .67f);
        faces.push_back(std::move(face));
    }
    std::sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.depth < b.depth; });
    QRectF box;
    for (const auto &f : faces)
        box = box.united(f.polygon.boundingRect());
    p.translate(80, 45);
    double zoom = std::min(148. / std::max(.1, box.width()), 80. / std::max(.1, box.height()));
    p.scale(zoom, zoom);
    p.translate(-box.center());
    for (const auto &f : faces) {
        p.setPen(QPen(f.color, .3 / zoom));
        p.setBrush(f.color);
        p.drawPolygon(f.polygon);
    }
}
void drawGraph(QPainter &p, const wb::PcgGraph &graph, const wb::PcgVolume &volume,
               const wb::AssetLibrary *lib) {
    wb::PcgPlan plan;
    QString error;
    if (!wb::compileGraph(graph, plan, error))
        return;
    constexpr int n = 36;
    QHash<QString, QColor> colors;
    QHash<QString, QImage> textures;
    for (const auto &node : plan.instructions)
        if (node.type == "output_surface") {
            const auto block = node.parameters["block"].toString("minecraft:stone");
            colors[node.id] = fallbackColor(block);
            if (lib) {
                auto entry = lib->catalog.constFind(block.section('[', 0, 0));
                if (entry != lib->catalog.cend() && entry->preview)
                    textures[node.id] = lib->thumbnails.value(entry->preview->thumbnail);
            }
        }
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            double u = (x + .5) / n, v = (z + .5) / n;
            double localX = u * (volume.max.x() - volume.min.x()),
                   localZ = v * (volume.max.z() - volume.min.z());
            double h =
                std::lerp(volume.min.y(), double(volume.max.y()), .3 + .3 * u + .15 * std::sin(v * 6.28));
            double mask =
                std::clamp(std::min({u, 1 - u, v, 1 - v}) *
                               std::min(volume.max.x() - volume.min.x(), volume.max.z() - volume.min.z()) /
                               std::max(1.f, volume.transition),
                           0., 1.);
            auto values =
                plan.evaluateValues({volume.min.x() + localX, volume.min.z() + localZ, h, h, 20. * u, localX,
                                     localZ, mask, volume.seed, 1, &volume.overrides});
            QColor color("#384b43");
            for (size_t k = 0; k < plan.instructions.size(); ++k) {
                auto &node = plan.instructions[k];
                auto value = std::clamp(values[k], 0., 1.) * mask;
                if (node.type == "output_surface" && value >= node.parameters["threshold"].toDouble(.5)) {
                    const auto texture = textures.value(node.id);
                    color = texture.isNull() ? colors.value(node.id)
                                             : texture.pixelColor(x % texture.width(), z % texture.height());
                }
                if (node.type == "output_scatter" &&
                    ((x * 31 + z * 57 + x * z * 13 + volume.seed) % 101) / 101. <
                        value * node.parameters["density"].toDouble(.5))
                    color = QColor("#8db55f");
            }
            const double light = .75 + .23 * u;
            p.fillRect(QRectF(45 + x * 2, 9 + z * 2, 2, 2),
                       QColor::fromRgbF(color.redF() * light, color.greenF() * light, color.blueF() * light));
        }
}
} // namespace
QString Editor::assetThumbnail(const QString &id, const QString &key) const {
    const QString cacheKey = id + "/" + key;
    if (auto cached = m_thumbnailCache.object(cacheKey))
        return *cached;
    QImage image(160, 90, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#20282a"));
    QPainter painter(&image);
    for (const auto &a : m_world.creation.assets)
        if (a.id == id)
            drawStructure(painter, a, m_world.assets.get());
    for (const auto &t : m_world.heights.instances)
        if (t.id == id && t.image)
            drawTerrain(painter, t);
    for (const auto &g : m_world.creation.graphs)
        if (g.id == id)
            drawGraph(painter, g, {}, m_world.assets.get());
    for (const auto &a : m_world.creation.pcgAssets)
        if (a.id == id)
            if (const auto *graph = wb::libraryGraph(m_world.creation, a.id))
                drawGraph(painter, *graph, a.volume, m_world.assets.get());
    painter.end();
    QBuffer bytes;
    bytes.open(QIODevice::WriteOnly);
    image.save(&bytes, "PNG");
    auto url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.data().toBase64());
    m_thumbnailCache.insert(cacheKey, new QString(url));
    return url;
}
