#pragma once
#include "Mesh.h"

namespace wb {
// The partition covers every column exactly once, excluding the exact chunk set.
std::vector<FarTile> planFarTiles(const World &world, const std::set<int> &exact, int focusX, int focusZ);
struct FarInputs {
    // Sparse edits are sampled even when they fall between the regular LOD samples.
    std::map<int, std::map<int, int>> columns; // z, x, highest explicit non-air block
    std::vector<QVector4D> colors;
};
FarInputs farInputs(const World &world);
MeshPtr buildFarMesh(const World &world, const FarTile &tile, const FarInputs &inputs, uint64_t revision);
bool visibleMesh(const ChunkMesh &mesh, const QMatrix4x4 &matrix);
} // namespace wb
