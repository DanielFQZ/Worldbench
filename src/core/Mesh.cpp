#include "Mesh.h"
#include <algorithm>
#include <bitset>
namespace wb {
MeshPtr buildChunkMesh(const World &w, int cx, int cz, uint64_t rev, int skirtMask) {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = rev;
    mesh->skirtMask = skirtMask;
    mesh->center = {float(cx * 16 + 8), 20, float(cz * 16 + 8)};
    mesh->lower = {float(cx * 16), float(World::MinY), float(cz * 16)};
    mesh->upper = {float(cx * 16 + 16), float(World::MaxY), float(cz * 16 + 16)};
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
    // Interior columns are provably invisible only for opaque, cullable base materials.
    // Custom resource models without those guarantees retain the general voxel path.
    bool buriedCull = true;
    std::set<Block> baseMaterials{Grass, Dirt, Stone, Sand, Bedrock};
    if (w.theme.enabled)
        for (const auto &band : w.theme.bands) {
            baseMaterials.insert(band.surface);
            baseMaterials.insert(band.subsurface);
        }
    for (Block b : baseMaterials) {
        const auto &m = *w.models[b];
        buriedCull &= m.full && !m.translucent &&
                      std::all_of(m.faces.begin(), m.faces.end(), [](const auto &f) { return f.cull >= 0; });
    }
    std::bitset<World::MaxY - World::MinY> exceptions[16][16];
    auto mark = [&](int x, int y, int z) {
        x -= cx * 16;
        z -= cz * 16;
        if (x >= 0 && x < 16 && z >= 0 && z < 16 && y >= World::MinY && y < World::MaxY)
            exceptions[z][x].set(y - World::MinY);
    };
    int maxY = std::max(40, w.seaLevel + 1);
    for (auto [k, v] : w.overrides) {
        int x, y, z;
        w.decode(k, x, y, z);
        if (floorDiv(x, 16) == cx && floorDiv(z, 16) == cz)
            maxY = std::max(maxY, y + 1);
        mark(x, y, z);
        for (const auto &n : normals)
            mark(x + n[0], y + n[1], z + n[2]);
    }
    for (int z = cz * 16; z < cz * 16 + 16; ++z)
        for (int x = cx * 16; x < cx * 16 + 16; ++x)
            maxY = std::max(maxY, heights[z - cz * 16 + 1][x - cx * 16 + 1] + 1);
    for (int z = cz * 16; z < cz * 16 + 16; ++z)
        for (int x = cx * 16; x < cx * 16 + 16; ++x) {
            if (!w.contains(x, 0, z))
                continue;
            const int lx = x - cx * 16, lz = z - cz * 16;
            const int surface =
                buriedCull ? std::min({heights[lz + 1][lx + 1], heights[lz][lx + 1], heights[lz + 2][lx + 1],
                                       heights[lz + 1][lx], heights[lz + 1][lx + 2]})
                           : World::MinY;
            for (int y = World::MinY; y < std::min(World::MaxY, maxY); ++y) {
                if (y > World::MinY && y < surface && !exceptions[lz][lx].test(y - World::MinY))
                    continue;
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
        }
    // Close the near/coarse boundary underneath the true terrain edge. This is
    // outside the editable data and is omitted on exact-to-exact interfaces.
    for (int side = 0; side < 4; ++side)
        if (skirtMask & (1 << side))
            for (int t = 0; t < 16; ++t) {
                const int x = cx * 16 + (side < 2 ? (side == 0 ? 0 : 15) : t);
                const int z = cz * 16 + (side >= 2 ? (side == 2 ? 0 : 15) : t);
                if (!w.contains(x, 0, z))
                    continue;
                const float h = float(std::max(w.height(x, z), w.seaLevel) + 1), low = float(World::MinY);
                QVector3D a(float(x), low, float(z)), b = a;
                if (side < 2) {
                    a.setX(float(x + (side == 1)));
                    b = a;
                    b.setZ(z + 1.f);
                } else {
                    a.setZ(float(z + (side == 3)));
                    b = a;
                    b.setX(x + 1.f);
                }
                QVector3D points[4]{a, b, {b.x(), h, b.z()}, {a.x(), h, a.z()}};
                QVector3D n(side == 0   ? -1.f
                            : side == 1 ? 1.f
                                        : 0.f,
                            0,
                            side == 2   ? -1.f
                            : side == 3 ? 1.f
                                        : 0.f);
                for (int i : tri)
                    mesh->opaque.push_back({points[i].x(), points[i].y(), points[i].z(), n.x(), n.y(), n.z(),
                                            .35f, .32f, .27f, 1, -1, -1});
            }
    // JSON elements can overhang their owning cell/chunk (for example leaves).
    // Culling uses the rendered geometry, never just the nominal chunk footprint.
    for (const auto *vertices : {&mesh->opaque, &mesh->transparent})
        for (const auto &v : *vertices) {
            mesh->lower.setX(std::min(mesh->lower.x(), v.x));
            mesh->lower.setY(std::min(mesh->lower.y(), v.y));
            mesh->lower.setZ(std::min(mesh->lower.z(), v.z));
            mesh->upper.setX(std::max(mesh->upper.x(), v.x));
            mesh->upper.setY(std::max(mesh->upper.y(), v.y));
            mesh->upper.setZ(std::max(mesh->upper.z(), v.z));
        }
    return mesh;
}
} // namespace wb
