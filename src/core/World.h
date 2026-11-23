#pragma once
#include "Assets.h"
#include "Environment.h"
#include "HeightDeltas.h"
#include "Heightmap.h"
#include "WorldCreation.h"
#include <QRect>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace wb {
using Block = uint32_t;
enum BuiltinBlock : uint32_t {
    Air,
    Grass,
    Dirt,
    Stone,
    Sand,
    Water,
    Planks,
    Glass,
    Slab,
    Bedrock,
    BlockCount
};
struct BlockInfo {
    const char *name;
    const char *minecraft;
    float r, g, b, a;
    bool full;
};
const BlockInfo &info(Block block);
struct Hit {
    bool valid = false;
    int x = 0, y = 0, z = 0;
    QVector3D position, normal;
    Block block = Air;
};
struct CellChange {
    uint64_t key;
    int before, after;
};
struct SculptLayer {
    QString id = "terrain-base-1", name = "基础地形层1";
    bool enabled = true;
    std::shared_ptr<HeightDeltas> deltas = std::make_shared<HeightDeltas>();
    bool operator==(const SculptLayer &o) const {
        return id == o.id && name == o.name && enabled == o.enabled && *deltas == *o.deltas;
    }
};
struct HeightChange {
    int index, before, after;
};
struct SurfaceBand {
    int minHeight = -63;
    QString name = "低地";
    Block surface = Grass, subsurface = Dirt;
    int depth = 4; // Total soil thickness, including the surface block.
    bool operator==(const SurfaceBand &) const = default;
};
struct SurfaceTheme {
    bool enabled = false, beaches = true;
    int noise = 0, scale = 32, seed = 2026;
    std::vector<SurfaceBand> bands{{}, {64, "高山", Stone, Stone, 4}};
    Block block(int x, int y, int z, int height, int seaLevel) const;
    bool operator==(const SurfaceTheme &) const = default;
};
bool validateTheme(const SurfaceTheme &theme, size_t paletteSize, QString &error);
QJsonObject saveTheme(const SurfaceTheme &theme);
bool loadTheme(const QJsonObject &object, SurfaceTheme &theme, size_t paletteSize, QString &error);
struct Command {
    std::vector<CellChange> cells;
    std::vector<HeightChange> heights;
    QString heightLayer;
    // Large region edits publish a prepared immutable-by-sharing layer snapshot.
    // Subsequent writes use HeightField's existing copy-on-write contract.
    std::shared_ptr<HeightDeltas> deltaBefore, deltaAfter;
    QRect heightArea;
    std::shared_ptr<const std::vector<SculptLayer>> layersBefore, layersAfter;
    QString label;
    std::shared_ptr<const std::vector<TerrainInstance>> terrainBefore, terrainAfter;
    std::shared_ptr<const SurfaceTheme> themeBefore, themeAfter;
    std::shared_ptr<const Environment> environmentBefore, environmentAfter;
    std::shared_ptr<const CreationState> creationBefore, creationAfter;
};
struct WorldOptions {
    int width = 128, seed = 2026, seaLevel = 12, baseHeight = 24, variation = 12;
    bool flat = false;
    int depth = 0;
};
// Immutable generated/dense base plus copy-on-write edited columns. Large untouched
// terrain never allocates a width*depth array; snapshots share the base and edits.
class HeightField {
  public:
    int width = 0, depth = 0, originX = 0, originZ = 0, seed = 0, baseHeight = 0, variation = 0;
    bool flat = true;
    std::shared_ptr<const std::vector<int16_t>> dense;
    std::vector<TerrainInstance> instances;
    std::vector<SculptLayer> layers{SculptLayer{}};
    QString activeLayer = "terrain-base-1";
    int layerIndex(const QString &id) const;
    int delta(int index) const;
    void restoreDelta(const QString &layer, int index, int value);
    void configure(const WorldOptions &o, int x, int z);
    size_t size() const { return size_t(width) * depth; }
    int16_t base(int index) const;
    int16_t operator[](int index) const;
    void set(int index, int value);
    void restore(int index, int value, bool overridden);
    void fill(int value);
    void assign(std::vector<int16_t> values);
    const HeightDeltas &changes() const {
        return *layers.at(size_t(std::max(0, layerIndex(activeLayer)))).deltas;
    }
    bool operator==(const HeightField &other) const;
};
struct BrushOptions {
    int shape = 0, falloff = 0, flattenMode = 0;
    bool fillHoles = false;
    int fillDepth = 8;
    bool filterEnabled = false;
    int minHeight = -63, maxHeight = 318, minSlope = 0, maxSlope = 90;
    int materialFilter = 0;
    QString filterBlock = "minecraft:stone";
    double decay = 0; // Fraction of the radius occupied by the transition band.
    bool selectionEnabled = false;
    int selectionMinX = 0, selectionMaxX = 0, selectionMinZ = 0, selectionMaxZ = 0;
};

class World {
  public:
    World();
    static constexpr int MinY = -64, MaxY = 320, ChunkSize = 16;
    int width = 128, depth = 128, originX = -64, originZ = -64, seed = 2026, seaLevel = 12;
    HeightField heights;
    SurfaceTheme theme;
    Environment environment;
    std::unordered_map<uint64_t, Block> overrides;
    std::vector<BlockState> palette;
    std::vector<std::shared_ptr<const BlockModel>> models;
    std::shared_ptr<const AssetLibrary> assets;
    CreationState creation;
    std::shared_ptr<const GeneratedCreation> generated;
    bool rebuildCreation(QString &error, const CreationProgress &progress = {});
    QStringList resourcePaths;
    QString resourceVersion;
    Block intern(const BlockState &state);
    void setAssets(std::shared_ptr<const AssetLibrary> library);
    static World create(int size = 128, int seed = 2026);
    static World create(const WorldOptions &options);
    bool isAir(Block block) const;
    bool contains(int x, int y, int z) const;
    int column(int x, int z) const;
    int height(int x, int z) const;
    uint64_t key(int x, int y, int z) const;
    void decode(uint64_t key, int &x, int &y, int &z) const;
    int chunksX() const { return (width + 15) / 16; }
    int chunksZ() const { return (depth + 15) / 16; }
    Block baseBlock(int x, int y, int z) const;
    Block block(int x, int y, int z) const;
    Block blockAtHeight(int x, int y, int z, int height) const;
    void setBlock(int x, int y, int z, Block value);
    Hit raycast(QVector3D origin, QVector3D direction, float maxDistance = 1500) const;
    void apply(const Command &command, bool forward);
};

class Stroke {
  public:
    explicit Stroke(World &world) : world(world), layer(world.heights.activeLayer) {}
    bool stamp(const Hit &hit, int tool, int radius, int strength, Block material, bool reverse,
               const BrushOptions &options = {});
    Command finish(const QString &label);

  private:
    void changeCell(int x, int y, int z, Block block);
    void changeHeight(int index, int height);
    void reshapeSurface(int x, int z, int height, const BrushOptions &options);
    World &world;
    QString layer;
    std::map<uint64_t, int> beforeCells;
    std::map<int, int> beforeHeights;
    std::map<int, double> m_accumulated;
    int flattenHeight = -9999;
    uint64_t changes = 0;
};
std::set<int> affectedChunks(const World &world, const Command &command);
int floorDiv(int value, int divisor);
} // namespace wb
