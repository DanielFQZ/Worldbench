#pragma once
#include "World.h"
#include <QRect>
#include <memory>
namespace wb {
struct SurfaceMaterials;
struct Vertex {
    float x, y, z, nx, ny, nz, r, g, b, a, u, v;
};
struct ChunkMesh {
    std::vector<Vertex> opaque, transparent, water;
    std::vector<uint32_t> opaqueIndices;
    QImage surfaceColors; // One resolved top-face color per column, including a one-column halo.
    uint64_t surfaceColorBytes() const {
        if (surfaceColors.isNull())
            return 0;
        uint64_t bytes = 0;
        int w = surfaceColors.width(), h = surfaceColors.height();
        for (;;) {
            bytes += uint64_t(w) * h * 4;
            if (w == 1 && h == 1)
                return bytes;
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
    }
    float surfaceError = 0;
    bool provisional = false; // Rebuild this tile at full quality after a gesture.
    uint64_t sampleCount = 0;
    uint64_t revision = 0;
    QRect surfaceCutout;
    size_t referenceVertices = 0;
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
    std::shared_ptr<const SurfaceMaterials> surfaceMaterials;
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
    QRect exactArea;
    QRect worldArea;
    float seaSurface = 0;
    size_t waterVertices = 0;
    uint64_t sourceRevision = 0, sampleCount = 0, bufferBytes = 0;
    uint64_t updatedSamples = 0, colorBytes = 0;
    float surfaceError = 0;
    // True while the scene is an interactive low-cost preview.  The world
    // data is still authoritative; this flag only describes the derived mesh.
    bool provisional = false;
    QString error;
};
MeshPtr buildChunkMesh(const World &world, int chunkX, int chunkZ, uint64_t revision,
                       bool creationOnly = false, bool isolatedAsset = false);
} // namespace wb
