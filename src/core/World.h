#pragma once
#include <QString>
#include <QVector3D>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace wb {
enum Block : uint8_t { Air, Grass, Dirt, Stone, Sand, Water, Planks, Glass, Slab, Bedrock, BlockCount };
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

class World {
  public:
    static constexpr int MinY = -64, MaxY = 320, ChunkSize = 16;
    int size = 128, originX = -64, originZ = -64, seed = 2026, seaLevel = 12;
    std::vector<int16_t> heights;
    std::unordered_map<uint32_t, Block> overrides;
    static World create(int size = 128, int seed = 2026);
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
    bool stamp(const Hit &hit, int tool, int radius, int strength, Block material, bool reverse);
    Command finish(const QString &label);

  private:
    void changeCell(int x, int y, int z, Block block);
    void changeHeight(int index, int height);
    World &world;
    std::map<uint32_t, int> beforeCells;
    std::map<int, int> beforeHeights;
    int flattenHeight = -9999;
};
int floorDiv(int value, int divisor);
} // namespace wb
