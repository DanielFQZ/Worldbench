#include "Heightmap.h"
#include <QJsonArray>
#include <QMap>
#include <QtEndian>
#include <climits>
namespace wb {
QJsonObject saveTerrain(const std::vector<TerrainInstance> &instances) {
    QJsonArray images, entries;
    QMap<const HeightmapData *, int> indices;
    auto index = [&](const auto &image) {
        if (!image)
            return -1;
        auto it = indices.find(image.get());
        if (it != indices.end())
            return it.value();
        const int id = int(images.size());
        indices.insert(image.get(), id);
        QByteArray bytes(qsizetype(image->values.size() * 2), Qt::Uninitialized);
        for (size_t i = 0; i < image->values.size(); ++i)
            qToLittleEndian(image->values[i], bytes.data() + i * 2);
        images.append(QJsonObject{{"width", image->width},
                                  {"depth", image->depth},
                                  {"pixels", QString::fromLatin1(bytes.toBase64())}});
        return id;
    };
    for (const auto &i : instances)
        entries.append(QJsonObject{{"id", i.id},
                                   {"name", i.name},
                                   {"sourceName", i.sourceName},
                                   {"maskName", i.maskName},
                                   {"image", index(i.image)},
                                   {"mask", index(i.mask)},
                                   {"x", i.x},
                                   {"z", i.z},
                                   {"width", i.width},
                                   {"depth", i.depth},
                                   {"low", i.low},
                                   {"high", i.high},
                                   {"blend", i.blend},
                                   {"opacity", i.opacity},
                                   {"feather", i.feather},
                                   {"enabled", i.enabled}});
    return {{"images", images}, {"instances", entries}};
}
bool loadTerrain(const QJsonObject &o, std::vector<TerrainInstance> &out, QString &error) {
    auto fail = [&] {
        error = "工程地貌实例或高度图数据无效";
        return false;
    };
    auto integer = [](QJsonValue v, int lo, int hi) {
        return v.isDouble() && v.toDouble() == v.toInt(INT_MIN) && v.toInt() >= lo && v.toInt() <= hi;
    };
    if (!o["images"].isArray() || !o["instances"].isArray() || o["images"].toArray().size() > 64 ||
        o["instances"].toArray().size() > 32)
        return fail();
    std::vector<std::shared_ptr<const HeightmapData>> images;
    size_t totalPixels = 0;
    for (auto value : o["images"].toArray()) {
        const auto a = value.toObject();
        if (!integer(a["width"], 1, 4096) || !integer(a["depth"], 1, 4096) || !a["pixels"].isString())
            return fail();
        const size_t count = size_t(a["width"].toInt()) * a["depth"].toInt();
        totalPixels += count;
        if (totalPixels > MaxTerrainPixels ||
            size_t(a["pixels"].toString().size()) != (count * 2 + 2) / 3 * 4)
            return fail();
        const auto bytes = QByteArray::fromBase64Encoding(a["pixels"].toString().toLatin1(),
                                                          QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes || size_t(bytes.decoded.size()) != count * 2)
            return fail();
        auto image = std::make_shared<HeightmapData>();
        image->width = a["width"].toInt();
        image->depth = a["depth"].toInt();
        image->values.resize(count);
        for (size_t i = 0; i < count; ++i)
            image->values[i] = qFromLittleEndian<uint16_t>(bytes.decoded.constData() + i * 2);
        images.push_back(std::move(image));
    }
    std::vector<TerrainInstance> next;
    for (auto value : o["instances"].toArray()) {
        const auto a = value.toObject();
        if (!integer(a["image"], 0, int(images.size()) - 1) ||
            !integer(a["mask"], -1, int(images.size()) - 1) || !integer(a["x"], -2000000, 2000000) ||
            !integer(a["z"], -2000000, 2000000) || !integer(a["width"], 1, 16384) ||
            !integer(a["depth"], 1, 16384) || !integer(a["low"], -63, 318) || !integer(a["high"], -63, 318) ||
            !integer(a["blend"], 0, 3) || !integer(a["opacity"], 0, 100) || !integer(a["feather"], 0, 50) ||
            !a["enabled"].isBool() || !a["id"].isString() || !a["name"].isString() ||
            !a["sourceName"].isString() || !a["maskName"].isString())
            return fail();
        TerrainInstance i;
        i.id = a["id"].toString();
        i.name = a["name"].toString();
        i.sourceName = a["sourceName"].toString();
        i.maskName = a["maskName"].toString();
        i.image = images[a["image"].toInt()];
        if (a["mask"].toInt() >= 0)
            i.mask = images[a["mask"].toInt()];
        i.x = a["x"].toInt();
        i.z = a["z"].toInt();
        i.width = a["width"].toInt();
        i.depth = a["depth"].toInt();
        i.low = a["low"].toInt();
        i.high = a["high"].toInt();
        i.blend = a["blend"].toInt();
        i.opacity = a["opacity"].toInt();
        i.feather = a["feather"].toInt();
        i.enabled = a["enabled"].toBool();
        next.push_back(std::move(i));
    }
    if (!validateTerrain(next, error))
        return false;
    out = std::move(next);
    return true;
}
} // namespace wb
