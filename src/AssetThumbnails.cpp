#include "Editor.h"
#include <QBuffer>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
QColor blockColor(const wb::AssetLibrary *lib, const QString &state) {
    const auto name = state.section('[', 0, 0);
    if (lib) {
        const auto entry = lib->catalog.constFind(name);
        if (entry != lib->catalog.cend() && entry->preview) {
            const auto image = lib->thumbnails.value(entry->preview->thumbnail);
            if (!image.isNull()) {
                auto color =
                    image.scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
                if (name.contains("leaves") || name.contains("grass"))
                    color = QColor::fromRgbF(color.redF() * .58, color.greenF() * .85, color.blueF() * .4);
                color.setAlpha(255);
                return color;
            }
        }
    }
    if (name.contains("leaves") || name.contains("grass"))
        return QColor("#779252");
    if (name.contains("log") || name.contains("planks") || name.contains("dirt"))
        return QColor("#896745");
    if (name.contains("sand"))
        return QColor("#d7c495");
    return QColor("#92999b");
}
void drawStructure(QPainter &p, const wb::StructureAsset &asset, const wb::AssetLibrary *lib) {
    const int step = std::max(1, (std::max({asset.width, asset.height, asset.depth}) + 29) / 30);
    const int w = (asset.width + step - 1) / step, h = (asset.height + step - 1) / step,
              d = (asset.depth + step - 1) / step;
    QHash<QString, QColor> palette;
    std::vector<QColor> cells(w * h * d);
    auto index = [&](int x, int y, int z) { return (y * d + z) * w + x; };
    for (const auto &b : asset.blocks) {
        if (b.state.endsWith(":air") || b.state.endsWith("_air"))
            continue;
        auto it = palette.constFind(b.state);
        if (it == palette.cend())
            it = palette.insert(b.state, blockColor(lib, b.state));
        cells[index(b.x / step, b.y / step, b.z / step)] = it.value();
    }
    const double scale = std::min(146. / (w + d), 80. / (.45 * (w + d) + h));
    auto point = [&](int x, int y, int z) {
        return QPointF(80 + scale * (x - z - (w - d) * .5),
                       45 + scale * (.45 * (x + z) - y - .225 * (w + d) + h * .5));
    };
    auto face = [&](QColor c, QPointF a, QPointF b, QPointF d, QPointF e) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(QPolygonF{a, b, d, e});
    };
    for (int y = 0; y < h; ++y)
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x) {
                const auto color = cells[index(x, y, z)];
                if (!color.isValid())
                    continue;
                if (z + 1 == d || !cells[index(x, y, z + 1)].isValid())
                    face(color.darker(125), point(x, y, z + 1), point(x + 1, y, z + 1),
                         point(x + 1, y + 1, z + 1), point(x, y + 1, z + 1));
                if (x + 1 == w || !cells[index(x + 1, y, z)].isValid())
                    face(color.darker(160), point(x + 1, y, z), point(x + 1, y, z + 1),
                         point(x + 1, y + 1, z + 1), point(x + 1, y + 1, z));
                if (y + 1 == h || !cells[index(x, y + 1, z)].isValid())
                    face(color, point(x, y + 1, z), point(x + 1, y + 1, z), point(x + 1, y + 1, z + 1),
                         point(x, y + 1, z + 1));
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
    for (const auto &node : plan.instructions)
        if (node.type == "output_surface")
            colors[node.id] = blockColor(lib, node.parameters["block"].toString("minecraft:stone"));
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
                if (node.type == "output_surface" && value >= node.parameters["threshold"].toDouble(.5))
                    color = colors.value(node.id);
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
            drawGraph(painter, a.graph, a.volume, m_world.assets.get());
    painter.end();
    QBuffer bytes;
    bytes.open(QIODevice::WriteOnly);
    image.save(&bytes, "PNG");
    auto url = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.data().toBase64());
    m_thumbnailCache.insert(cacheKey, new QString(url));
    return url;
}
