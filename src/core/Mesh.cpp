#include "Mesh.h"
#include <algorithm>
namespace wb {
MeshPtr buildChunkMesh(const World &w, int cx, int cz, uint64_t rev) {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = rev;
    mesh->center = {float(cx * 16 + 8), 20, float(cz * 16 + 8)};
    const int normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    int maxY = std::max(40, w.seaLevel + 1);
    for (auto [k, v] : w.overrides) {
        int x, y, z;
        w.decode(k, x, y, z);
        if (floorDiv(x, 16) == cx && floorDiv(z, 16) == cz)
            maxY = std::max(maxY, y + 1);
    }
    for (int z = cz * 16; z < cz * 16 + 16; ++z)
        for (int x = cx * 16; x < cx * 16 + 16; ++x)
            maxY = std::max(maxY, w.height(x, z) + 1);
    for (int y = World::MinY; y < std::min(World::MaxY, maxY); ++y)
        for (int z = cz * 16; z < cz * 16 + 16; ++z)
            for (int x = cx * 16; x < cx * 16 + 16; ++x) {
                const Block b = w.block(x, y, z);
                if (b == Air)
                    continue;
                const auto &m = *w.models[b];
                auto &vertices = m.translucent ? mesh->transparent : mesh->opaque;
                for (const auto &face : m.faces) {
                    if (face.cull >= 0) {
                        const auto &n = normals[face.cull];
                        Block nb = w.block(x + n[0], y + n[1], z + n[2]);
                        const auto &neighbor = *w.models[nb];
                        if ((nb == b && m.translucent) ||
                            (neighbor.full && !neighbor.translucent && nb != Air))
                            continue;
                    }
                    for (int t : tri) {
                        const auto &p = face.points[t];
                        const auto &n = face.normal;
                        const auto &c = face.color;
                        vertices.push_back({x + p.x(), y + p.y(), z + p.z(), n.x(), n.y(), n.z(), c.x(),
                                            c.y(), c.z(), c.w(), face.uv[t].x(), face.uv[t].y()});
                    }
                }
            }
    return mesh;
}
} // namespace wb
