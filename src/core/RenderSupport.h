#pragma once
#include "Mesh.h"

namespace wb {
struct SurfaceMaterials {
    std::vector<QVector4D> colors;
};
QVector4D surfaceColor(const World &world, Block block);
SurfaceMaterials surfaceMaterials(const World &world);
bool visibleMesh(const ChunkMesh &mesh, const QMatrix4x4 &matrix);
} // namespace wb
