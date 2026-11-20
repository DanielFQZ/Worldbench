#pragma once
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>
namespace wb {
struct HeightmapData {
    int width = 0, depth = 0;
    std::vector<uint16_t> values;
    double sample(double u, double v) const;
    bool operator==(const HeightmapData &) const = default;
};
struct TerrainInstance {
    QString id, name, sourceName, maskName;
    std::shared_ptr<const HeightmapData> image, mask;
    int x = 0, z = 0, width = 128, depth = 128;
    int low = 24, high = 80, blend = 0, opacity = 100, feather = 10;
    bool enabled = true;
    double apply(double base, int worldX, int worldZ) const;
    bool operator==(const TerrainInstance &other) const;
};
inline constexpr size_t MaxTerrainPixels = 4 * 1024 * 1024;
std::shared_ptr<const HeightmapData> loadHeightmap(const QString &path, QString &error);
bool validateTerrain(const std::vector<TerrainInstance> &instances, QString &error);
QJsonObject saveTerrain(const std::vector<TerrainInstance> &instances);
bool loadTerrain(const QJsonObject &object, std::vector<TerrainInstance> &instances, QString &error);
} // namespace wb
