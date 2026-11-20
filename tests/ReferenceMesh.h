#pragma once
#include "core/Mesh.h"
#include <algorithm>
namespace wb {
inline MeshPtr referenceChunkMesh(const World &w, int cx, int cz, uint64_t rev) {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = rev;
    mesh->center = {float(cx * 16 + 8), 20, float(cz * 16 + 8)};
    const int normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    // Evaluate procedural heights once for this chunk and its face-neighbour border.
    int heights[18][18];
    for (int z = 0; z < 18; ++z)
        for (int x = 0; x < 18; ++x)
            heights[z][x] = w.height(cx * 16 + x - 1, cz * 16 + z - 1);
    auto blockAt = [&](int x, int y, int z) -> Block {
        const int h = heights[z - cz * 16 + 1][x - cx * 16 + 1];
        return w.blockAtHeight(x, y, z, h);
    };
    int maxY = std::max(40, w.seaLevel + 1);
    for (auto [k, v] : w.overrides) {
        int x, y, z;
        w.decode(k, x, y, z);
        if (floorDiv(x, 16) == cx && floorDiv(z, 16) == cz)
            maxY = std::max(maxY, y + 1);
    }
    for (int z = cz * 16; z < cz * 16 + 16; ++z)
        for (int x = cx * 16; x < cx * 16 + 16; ++x)
            maxY = std::max(maxY, heights[z - cz * 16 + 1][x - cx * 16 + 1] + 1);
    for (int y = World::MinY; y < std::min(World::MaxY, maxY); ++y)
        for (int z = cz * 16; z < cz * 16 + 16; ++z)
            for (int x = cx * 16; x < cx * 16 + 16; ++x) {
                const Block b = blockAt(x, y, z);
                if (b == Air)
                    continue;
                const auto &m = *w.models[b];
                auto &vertices = m.translucent ? mesh->transparent : mesh->opaque;
                for (const auto &face : m.faces) {
                    if (face.cull >= 0) {
                        const auto &n = normals[face.cull];
                        Block nb = blockAt(x + n[0], y + n[1], z + n[2]);
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

inline bool sameMesh(const wb::MeshPtr &a, const wb::MeshPtr &b) {
    auto canonical = [](const std::vector<wb::Vertex> &v) {
        std::vector<std::array<float, 13>> out;
        for (const auto &p : v)
            out.push_back({p.x, p.y, p.z, p.nx, p.ny, p.nz, p.r, p.g, p.b, p.a, p.u, p.v});
        std::sort(out.begin(), out.end());
        return out;
    };
    return canonical(a->opaque) == canonical(b->opaque) &&
           canonical(a->transparent) == canonical(b->transparent);
}
