#pragma once
#include "Assets.h"
#include "Heightmap.h"
#include <QString>
#include <QVector3D>
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
struct HeightChange {
    int index, before, after;
    bool beforeOverride = false, afterOverride = true;
};
struct Command {
    std::vector<CellChange> cells;
    std::vector<HeightChange> heights;
    QString label;
    std::shared_ptr<const std::vector<TerrainInstance>> terrainBefore, terrainAfter;
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
    void configure(const WorldOptions &o, int x, int z);
    size_t size() const { return size_t(width) * depth; }
    int16_t base(int index) const;
    int16_t operator[](int index) const;
    void set(int index, int value);
    void restore(int index, int value, bool overridden);
    void fill(int value);
    void assign(std::vector<int16_t> values);
    const std::map<int, int16_t> &changes() const { return *m_edits; }
    bool operator==(const HeightField &other) const;

  private:
    std::shared_ptr<std::map<int, int16_t>> m_edits = std::make_shared<std::map<int, int16_t>>();
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
    std::unordered_map<uint64_t, Block> overrides;
    std::vector<BlockState> palette;
    std::vector<std::shared_ptr<const BlockModel>> models;
    std::shared_ptr<const AssetLibrary> assets;
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
    void setBlock(int x, int y, int z, Block value);
    Hit raycast(QVector3D origin, QVector3D direction, float maxDistance = 1500) const;
    void apply(const Command &command, bool forward);
};

class Stroke {
  public:
    explicit Stroke(World &world) : world(world) {}
    bool stamp(const Hit &hit, int tool, int radius, int strength, Block material, bool reverse,
               const BrushOptions &options = {});
    Command finish(const QString &label);

  private:
    void changeCell(int x, int y, int z, Block block);
    void changeHeight(int index, int height);
    void reshapeSurface(int x, int z, int height, const BrushOptions &options);
    World &world;
    std::map<uint64_t, int> beforeCells;
    std::map<int, int> beforeHeights;
    std::map<int, bool> beforeHeightOverrides;
    std::map<int, double> m_accumulated;
    int flattenHeight = -9999;
    uint64_t changes = 0;
};
std::set<int> affectedChunks(const World &world, const Command &command);
int floorDiv(int value, int divisor);
} // namespace wb
