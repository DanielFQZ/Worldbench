#include "Mesh.h"
#include <algorithm>
namespace wb {
MeshPtr buildChunkMesh(const World &w, int cx, int cz, uint64_t rev) {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = rev;
    mesh->center = {float(cx * 16 + 8), 20, float(cz * 16 + 8)};
    const int normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const float corners[6][4][3] = {
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}};
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
                const auto &m = info(b);
                auto &vertices = m.a < 1 ? mesh->transparent : mesh->opaque;
                for (int face = 0; face < 6; ++face) {
                    const auto &n = normals[face];
                    Block nb = w.block(x + n[0], y + n[1], z + n[2]);
                    if (b != Slab && nb == b && m.a < 1)
                        continue;
                    if (nb != Air && nb != Slab && info(nb).a == 1 && !(b == Slab && face == 2))
                        continue;
                    for (int t : tri) {
                        const auto &c = corners[face][t];
                        vertices.push_back({x + c[0], y + c[1] * (b == Slab ? .5f : 1.f), z + c[2],
                                            float(n[0]), float(n[1]), float(n[2]), m.r, m.g, m.b, m.a});
                    }
                }
            }
    return mesh;
}
} // namespace wb
