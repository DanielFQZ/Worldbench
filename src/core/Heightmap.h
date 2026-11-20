#pragma once
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector3D>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace wb {
struct HeightmapData {
    int width = 0, depth = 0;
    std::vector<uint16_t> values;
    struct Bounds {
        int x0, z0, x1, z1;
        uint16_t low, high;
        int children[4]{-1, -1, -1, -1};
    };
    std::vector<Bounds> bounds;
    void buildBounds();
    bool raySample(const std::array<double, 3> &origin, const std::array<double, 3> &direction, double &t,
                   double &u, double &v) const;
    double sample(double u, double v) const;
    bool operator==(const HeightmapData &o) const {
        return width == o.width && depth == o.depth && values == o.values;
    }
};
struct TerrainInstance {
    QString id, name, sourceName, maskName;
    std::shared_ptr<const HeightmapData> image, mask;
    int x = 0, z = 0, width = 128, depth = 128;
    int y = 0, rotationX = 0, rotationY = 0, rotationZ = 0;
    int scaleX = 100, scaleY = 100, scaleZ = 100;
    QVector3D pivot() const;
    QRectF bounds() const;
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
