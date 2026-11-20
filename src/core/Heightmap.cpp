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
double TerrainInstance::apply(double base, int worldX, int worldZ) const {
    if (!enabled || !image || worldX < x || worldZ < z || worldX >= x + width || worldZ >= z + depth)
        return base;
    const double u = width == 1 ? .5 : double(worldX - x) / (width - 1);
    const double v = depth == 1 ? .5 : double(worldZ - z) / (depth - 1);
    double alpha = opacity / 100.0;
    if (mask)
        alpha *= mask->sample(u, v);
    if (feather > 0) {
        const double edge = std::min({u, v, 1 - u, 1 - v});
        const double t = std::clamp(edge / (feather / 100.0), 0.0, 1.0);
        alpha *= t * t * (3 - 2 * t);
    }
    const double value = std::lerp(double(low), double(high), image->sample(u, v));
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
           same(image, o.image) && same(mask, o.mask);
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
            i.feather < 0 || i.feather > 50) {
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
