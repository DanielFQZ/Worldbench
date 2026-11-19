#pragma once
#include "World.h"
#include <memory>
namespace wb {
struct Vertex {
    float x, y, z, nx, ny, nz, r, g, b, a, u, v;
};
struct ChunkMesh {
    std::vector<Vertex> opaque, transparent;
    uint64_t revision = 0;
    QVector3D center;
};
using MeshPtr = std::shared_ptr<const ChunkMesh>;
struct MeshScene {
    std::vector<MeshPtr> chunks;
    std::vector<int> chunkIds;
    uint64_t revision = 0;
    size_t vertices = 0;
    int updatedChunks = 0;
    qint64 buildMilliseconds = 0;
    std::shared_ptr<const AssetLibrary> assets;
};
MeshPtr buildChunkMesh(const World &world, int chunkX, int chunkZ, uint64_t revision);
} // namespace wb
