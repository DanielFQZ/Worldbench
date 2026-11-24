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
    struct SurfaceBase {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        size_t referenceVertices;
    };
    std::shared_ptr<const SurfaceBase> beforeHoles;
    uint64_t clipCacheBytes() const {
        return beforeHoles ? beforeHoles->vertices.size() * sizeof(Vertex) +
                                 beforeHoles->indices.size() * sizeof(uint32_t)
                           : 0;
    }
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
    struct HeightRange {
        short low = 32767, high = -32768;
    };
    std::vector<HeightRange> terrainRanges; // One range per 16x16 columns, before the exact cutout.
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
    struct Object {
        QString id, ownerId;
        AssetPreset placement;
        MeshPtr mesh;
        std::vector<MeshPtr> lods;
        std::shared_ptr<const std::vector<std::pair<int64_t, Block>>> shape;
    };
    struct Cluster {
        MeshPtr mesh;
        std::vector<size_t> members;
    };
    std::vector<Cluster> clusters;
    // Tight occupied bounds per edited 16-cube, indexed first by horizontal chunk.
    std::map<std::pair<int, int>, std::map<int, std::pair<QVector3D, QVector3D>>> editedBounds;
    std::vector<Object> objects;
    std::shared_ptr<const GeneratedCreation> generated;
    CreationState creation;
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
                       bool creationOnly = false, bool isolatedAsset = false, bool omitObjects = false);
std::vector<MeshScene::Object> buildCreationMeshes(const World &world, uint64_t revision,
                                                   std::vector<MeshScene::Cluster> *clusters = nullptr,
                                                   const MeshScene *previous = nullptr,
                                                   const QString &onlyPlacement = {});
MeshPtr buildTerrainAssetPreview(const TerrainInstance &terrain, int maxSamples = 129);
MeshPtr buildAssetGhost(World world, const StructureAsset &asset);
} // namespace wb
