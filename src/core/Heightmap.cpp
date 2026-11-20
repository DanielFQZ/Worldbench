#include "Heightmap.h"
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace wb {
double HeightmapData::sample(double u, double v) const {
    const double px = std::clamp(u, 0.0, 1.0) * (width - 1), pz = std::clamp(v, 0.0, 1.0) * (depth - 1);
    const int x = int(px), z = int(pz), x1 = std::min(x + 1, width - 1), z1 = std::min(z + 1, depth - 1);
    const double a = std::lerp(double(values[z * width + x]), double(values[z * width + x1]), px - x);
    const double b = std::lerp(double(values[z1 * width + x]), double(values[z1 * width + x1]), px - x);
    return std::lerp(a, b, pz - z) / 65535.0;
}
QVector3D TerrainInstance::pivot() const {
    return {float(x + (width - 1) * .5), float(low + y), float(z + (depth - 1) * .5)};
}
double TerrainInstance::apply(double base, int worldX, int worldZ) const {
    if (!enabled || !image || opacity == 0)
        return base;
    const double sx = std::max(1, width - 1) * scaleX / 100., sz = std::max(1, depth - 1) * scaleZ / 100.;
    const double sy = (high - low) * scaleY / 100.;
    const double cx = x + (width - 1) * .5, cz = z + (depth - 1) * .5;
    const double pi = 3.14159265358979323846 / 180;
    const double ay = rotationY * pi, c = std::cos(ay), sn = std::sin(ay);
    double u, v, value;
    if (rotationX == 0 && rotationZ == 0) {
        const double dx = worldX - cx, dz = worldZ - cz;
        u = (c * dx - sn * dz) / sx + .5;
        v = (sn * dx + c * dz) / sz + .5;
        if (u < -1e-8 || u > 1 + 1e-8 || v < -1e-8 || v > 1 + 1e-8)
            return base;
        value = low + y + sy * image->sample(u, v);
    } else {
        const double ax = rotationX * pi, az = rotationZ * pi, ca = std::cos(ax), sa = std::sin(ax),
                     cb = std::cos(az), sb = std::sin(az);
        // R = Ry * Rx * Rz; inverse transforms a world vertical ray into the sampled surface.
        const double r[3][3] = {{c * cb + sn * sa * sb, -c * sb + sn * sa * cb, sn * ca},
                                {ca * sb, ca * cb, -sa},
                                {-sn * cb + c * sa * sb, sn * sb + c * sa * cb, c * ca}};
        const double vy = std::max(.0001, sy);
        const double top =
            low + y + std::abs(r[1][0]) * sx * .5 + std::abs(r[1][2]) * sz * .5 + std::abs(r[1][1]) * vy + 1;
        const double delta[3] = {worldX - cx, top - low - y, worldZ - cz};
        std::array<double, 3> o{}, d{};
        const double scales[3] = {sx, vy, sz};
        for (int k = 0; k < 3; ++k) {
            o[k] = (r[0][k] * delta[0] + r[1][k] * delta[1] + r[2][k] * delta[2]) / scales[k];
            d[k] = -r[1][k] / scales[k];
        }
        o[0] += .5;
        o[2] += .5;
        double t = 1e30;
        if (!image->raySample(o, d, t, u, v))
            return base;
        value = top - t;
    }
    double alpha = opacity / 100.;
    if (mask)
        alpha *= mask->sample(u, v);
    if (feather > 0) {
        double t = std::clamp(std::min({u, v, 1 - u, 1 - v}) / (feather / 100.), 0., 1.);
        alpha *= t * t * (3 - 2 * t);
    }
    const double target = blend == 1   ? base + value
                          : blend == 2 ? std::max(base, value)
                          : blend == 3 ? std::min(base, value)
                                       : value;
    return std::lerp(base, target, alpha);
}
bool TerrainInstance::operator==(const TerrainInstance &o) const {
    auto same = [](const auto &a, const auto &b) { return a == b || (a && b && *a == *b); };
    return id == o.id && name == o.name && sourceName == o.sourceName && maskName == o.maskName && x == o.x &&
           z == o.z && width == o.width && depth == o.depth && low == o.low && high == o.high &&
           blend == o.blend && opacity == o.opacity && feather == o.feather && enabled == o.enabled &&
           same(image, o.image) && same(mask, o.mask) && y == o.y && rotationX == o.rotationX &&
           rotationY == o.rotationY && rotationZ == o.rotationZ && scaleX == o.scaleX && scaleY == o.scaleY &&
           scaleZ == o.scaleZ;
}
std::shared_ptr<const HeightmapData> loadHeightmap(const QString &path, QString &error) {
    auto fail = [&](const QString &message) -> std::shared_ptr<const HeightmapData> {
        error = message;
        return {};
    };
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 32 * 1024 * 1024)
        return fail("请选择不超过 32 MiB 的灰度 PNG 文件");
    QImageReader reader(path);
    const QSize size = reader.size();
    if (reader.format().toLower() != "png" || size.width() < 1 || size.height() < 1 || size.width() > 4096 ||
        size.height() > 4096 || qint64(size.width()) * size.height() > qint64(MaxTerrainPixels))
        return fail("本版支持 8/16 位灰度 PNG，单边最多 4096，总像素最多 4194304");
    auto image = reader.read();
    if (image.isNull())
        return fail("高度图读取失败：" + reader.errorString());
    auto data = std::make_shared<HeightmapData>();
    data->width = image.width();
    data->depth = image.height();
    data->values.reserve(size_t(data->width) * data->depth);
    if (image.format() == QImage::Format_Grayscale16) {
        for (int z = 0; z < data->depth; ++z) {
            const auto *line = reinterpret_cast<const quint16 *>(image.constScanLine(z));
            data->values.insert(data->values.end(), line, line + data->width);
        }
    } else {
        image = image.convertToFormat(QImage::Format_RGBA64);
        for (int z = 0; z < data->depth; ++z) {
            const auto *line = reinterpret_cast<const QRgba64 *>(image.constScanLine(z));
            for (int x = 0; x < data->width; ++x) {
                if (line[x].red() != line[x].green() || line[x].red() != line[x].blue() ||
                    line[x].alpha() != 65535)
                    return fail("请使用不带透明度的灰度图（R、G、B 相同）；不自动转换彩色图");
                data->values.push_back(line[x].red());
            }
        }
    }
    data->buildBounds();
    return data;
}
bool validateTerrain(const std::vector<TerrainInstance> &instances, QString &error) {
    QSet<QString> ids;
    QSet<const HeightmapData *> images;
    size_t pixels = 0;
    if (instances.size() > 32) {
        error = "本版最多 32 个地貌实例";
        return false;
    }
    for (const auto &i : instances) {
        if (i.id.isEmpty() || i.id.size() > 80 || i.id == "terrain-base-1" || ids.contains(i.id) ||
            i.name.isEmpty() || i.name.size() > 128 || i.sourceName.size() > 256 || i.maskName.size() > 256 ||
            !i.image || i.x < -2000000 || i.x > 2000000 || i.z < -2000000 || i.z > 2000000 || i.width < 1 ||
            i.width > 16384 || i.depth < 1 || i.depth > 16384 || i.low < -63 || i.high > 318 ||
            i.low > i.high || i.blend < 0 || i.blend > 3 || i.opacity < 0 || i.opacity > 100 ||
            i.feather < 0 || i.feather > 50 || i.y < -10000 || i.y > 10000 || std::abs(i.rotationX) > 180 ||
            std::abs(i.rotationY) > 180 || std::abs(i.rotationZ) > 180 || i.scaleX < 1 || i.scaleX > 1600 ||
            i.scaleY < 1 || i.scaleY > 1600 || i.scaleZ < 1 || i.scaleZ > 1600) {
            error = "地貌实例参数或标识不合法";
            return false;
        }
        ids.insert(i.id);
        for (auto image : {i.image, i.mask}) {
            if (!image || images.contains(image.get()))
                continue;
            if (image->width < 1 || image->depth < 1 || image->width > 4096 || image->depth > 4096 ||
                image->values.size() != size_t(image->width) * image->depth) {
                error = "高度图数据不合法";
                return false;
            }
            images.insert(image.get());
            pixels += image->values.size();
        }
    }
    if (pixels > MaxTerrainPixels) {
        error = "本版高度图与蒙版合计最多 4194304 像素";
        return false;
    }
    return true;
}
} // namespace wb
