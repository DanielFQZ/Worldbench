#pragma once
#include "World.h"
#include <QRect>
#include <memory>
namespace wb {
struct Vertex {
    float x, y, z, nx, ny, nz, r, g, b, a, u, v;
};
struct ChunkMesh {
    std::vector<Vertex> opaque, transparent;
    uint64_t revision = 0;
    int skirtMask = 0;
    QVector3D center;
    QVector3D lower, upper;
};
using MeshPtr = std::shared_ptr<const ChunkMesh>;
struct FarTile {
    QRect area;
    int step = 1;
    MeshPtr mesh;
};
struct MeshScene {
    std::vector<MeshPtr> chunks;
    std::vector<int> chunkIds;
    std::vector<FarTile> farTiles;
    uint64_t revision = 0;
    size_t vertices = 0;
    int updatedChunks = 0;
    qint64 buildMilliseconds = 0;
    qint64 farMilliseconds = 0;
    int updatedFarTiles = 0;
    std::shared_ptr<const AssetLibrary> assets;
};
MeshPtr buildChunkMesh(const World &world, int chunkX, int chunkZ, uint64_t revision, int skirtMask = 0);
} // namespace wb
