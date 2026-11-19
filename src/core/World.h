#pragma once
#include "Assets.h"
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
    uint32_t key;
    int before, after;
};
struct HeightChange {
    int index, before, after;
};
struct Command {
    std::vector<CellChange> cells;
    std::vector<HeightChange> heights;
    QString label;
};
struct WorldOptions {
    int size = 128, seed = 2026, seaLevel = 12, baseHeight = 24, variation = 12;
    bool flat = false;
};
struct BrushOptions {
    int shape = 0, falloff = 0, flattenMode = 0;
    bool fillHoles = false;
    int fillDepth = 8;
    bool filterEnabled = false;
    int minHeight = -63, maxHeight = 318, minSlope = 0, maxSlope = 90;
    int materialFilter = 0;
    QString filterBlock = "minecraft:stone";
};

class World {
  public:
    World();
    static constexpr int MinY = -64, MaxY = 320, ChunkSize = 16;
    int size = 128, originX = -64, originZ = -64, seed = 2026, seaLevel = 12;
    std::vector<int16_t> heights;
    std::unordered_map<uint32_t, Block> overrides;
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
    uint32_t key(int x, int y, int z) const;
    void decode(uint32_t key, int &x, int &y, int &z) const;
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
    std::map<uint32_t, int> beforeCells;
    std::map<int, int> beforeHeights;
    int flattenHeight = -9999;
    uint64_t changes = 0;
};
std::set<int> affectedChunks(const World &world, const Command &command);
int floorDiv(int value, int divisor);
} // namespace wb
