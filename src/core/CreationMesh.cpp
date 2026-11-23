#include "Mesh.h"
#include "RenderSupport.h"
#include <QMatrix4x4>
#include <meshoptimizer.h>
#include <numeric>
#include <stdexcept>

namespace wb {
namespace {
constexpr int normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
constexpr int triangles[6] = {0, 1, 2, 0, 2, 3};
void bounds(ChunkMesh &mesh) {
    mesh.lower = {1e9f, 1e9f, 1e9f};
    mesh.upper = {-1e9f, -1e9f, -1e9f};
    for (const auto *list : {&mesh.opaque, &mesh.transparent, &mesh.water})
        for (const auto &v : *list)
            for (int a = 0; a < 3; ++a) {
                const float p = a == 0 ? v.x : a == 1 ? v.y : v.z;
                mesh.lower[a] = std::min(mesh.lower[a], p);
                mesh.upper[a] = std::max(mesh.upper[a], p);
            }
    mesh.center = (mesh.lower + mesh.upper) * .5f;
}
template <class Sample>
void cellMesh(const World &w, ChunkMesh &mesh, int x, int y, int z, Block b, Sample sample) {
    if (w.isAir(b))
        return;
    const auto &model = *w.models[b];
    auto &out = b == Water ? mesh.water : model.translucent ? mesh.transparent : mesh.opaque;
    if ((mesh.opaque.size() + mesh.transparent.size()) * sizeof(Vertex) > 128ull * 1024 * 1024)
        throw std::runtime_error("单资产显示网格超过128MiB预算");
    for (const auto &face : model.faces) {
        if (face.cull >= 0) {
            const auto &n = normals[face.cull];
            Block nb = sample(x + n[0], y + n[1], z + n[2]);
            const auto &other = *w.models[nb];
            if (nb != Air && model.hidesFace(other, nb == b))
                continue;
        }
        for (int t : triangles) {
            const auto &p = face.points[t];
            const auto &n = face.normal;
            const auto &c = face.color;
            out.push_back({x + p.x(), y + p.y(), z + p.z(), n.x(), n.y(), n.z(), c.x(), c.y(), c.z(), c.w(),
                           face.uv[t].x(), face.uv[t].y()});
        }
    }
}
MeshPtr simplify(const World &w, const MeshPtr &source, float ratio, float error) {
    if (source->opaque.size() < 72)
        return source;
    auto mesh = std::make_shared<ChunkMesh>(*source);
    auto &v = mesh->opaque;
    // Resolve atlas texels to colors before changing topology. Distant LODs
    // must not interpolate UVs across unrelated blocks in the resource atlas.
    const QImage atlas = w.assets ? w.assets->atlas : QImage{};
    for (size_t face = 0; face < v.size(); face += 6) {
        QVector3D color(1, 1, 1);
        if (v[face].u >= 0 && !atlas.isNull()) {
            float u0 = 1, v0 = 1, u1 = 0, v1 = 0;
            for (size_t i = face; i < std::min(face + 6, v.size()); ++i) {
                u0 = std::min(u0, v[i].u);
                u1 = std::max(u1, v[i].u);
                v0 = std::min(v0, v[i].v);
                v1 = std::max(v1, v[i].v);
            }
            QVector3D sum;
            float weight = 0;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    auto c =
                        atlas.pixelColor(std::clamp(int((u0 + (u1 - u0) * (x + .5f) / 4) * atlas.width()), 0,
                                                    atlas.width() - 1),
                                         std::clamp(int((v0 + (v1 - v0) * (y + .5f) / 4) * atlas.height()), 0,
                                                    atlas.height() - 1));
                    const float a = c.alphaF();
                    sum += QVector3D(c.redF(), c.greenF(), c.blueF()) * a;
                    weight += a;
                }
            if (weight > 0)
                color = sum / weight;
        }
        for (size_t i = face; i < std::min(face + 6, v.size()); ++i) {
            v[i].r *= color.x();
            v[i].g *= color.y();
            v[i].b *= color.z();
            v[i].u = v[i].v = -1;
        }
    }
    // Distant colors are averaged at welded positions. Keeping every atlas
    // color boundary as a topology seam would lock most voxel edges.
    std::vector<unsigned> remap(v.size()), indices(v.size());
    meshopt_Stream streams[] = {{&v[0].x, 12, sizeof(Vertex)}};
    size_t unique = meshopt_generateVertexRemapMulti(remap.data(), nullptr, v.size(), v.size(), streams, 1);
    std::vector<Vertex> welded(unique);
    meshopt_remapVertexBuffer(welded.data(), v.data(), v.size(), sizeof(Vertex), remap.data());
    meshopt_remapIndexBuffer(indices.data(), nullptr, v.size(), remap.data());
    std::vector<unsigned> contributions(unique);
    for (auto &vertex : welded)
        vertex.r = vertex.g = vertex.b = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        auto &vertex = welded[remap[i]];
        vertex.r += v[i].r;
        vertex.g += v[i].g;
        vertex.b += v[i].b;
        ++contributions[remap[i]];
    }
    for (size_t i = 0; i < unique; ++i) {
        welded[i].r /= contributions[i];
        welded[i].g /= contributions[i];
        welded[i].b /= contributions[i];
    }
    v = std::move(welded);
    mesh->opaqueIndices.resize(indices.size());
    float achieved = 0;
    size_t count = meshopt_simplify(
        mesh->opaqueIndices.data(), indices.data(), indices.size(), &v[0].x, v.size(), sizeof(Vertex),
        std::max(size_t(36), size_t(indices.size() * ratio) / 3 * 3), error, 0, &achieved);
    if (count < 3) {
        mesh->opaqueIndices = indices;
        count = indices.size();
        achieved = 0;
    }
    mesh->opaqueIndices.resize(count);
    mesh->surfaceError = achieved * meshopt_simplifyScale(&v[0].x, v.size(), sizeof(Vertex));
    // Accumulate smooth normals only for the reduced representation.
    for (auto &vertex : v)
        vertex.nx = vertex.ny = vertex.nz = 0;
    for (size_t i = 0; i < count; i += 3) {
        auto &a = v[mesh->opaqueIndices[i]], &b = v[mesh->opaqueIndices[i + 1]],
             &c = v[mesh->opaqueIndices[i + 2]];
        auto n = QVector3D::crossProduct(QVector3D(b.x - a.x, b.y - a.y, b.z - a.z),
                                         QVector3D(c.x - a.x, c.y - a.y, c.z - a.z));
        for (auto *p : {&a, &b, &c}) {
            p->nx += n.x();
            p->ny += n.y();
            p->nz += n.z();
        }
    }
    for (auto &p : v) {
        auto n = QVector3D(p.nx, p.ny, p.nz).normalized();
        p.nx = n.x();
        p.ny = n.y();
        p.nz = n.z();
    }
    // Compact the vertex buffer as well as the triangle list.
    std::vector<Vertex> compact(v.size());
    size_t used = meshopt_optimizeVertexFetch(compact.data(), mesh->opaqueIndices.data(), count, v.data(),
                                              v.size(), sizeof(Vertex));
    compact.resize(used);
    v = std::move(compact);
    return mesh;
}
} // namespace

std::vector<MeshScene::Object> buildCreationMeshes(const World &w, uint64_t revision) {
    std::vector<MeshScene::Object> result;
    if (!w.generated)
        return result;
    std::vector<std::vector<uint64_t>> keys(w.generated->objects.size());
    for (auto [key, owner] : w.generated->objectCells)
        if (owner < keys.size() && !w.overrides.contains(key))
            keys[owner].push_back(key);
    std::unordered_map<int, int> heights;
    size_t meshBytes = 0;
    constexpr size_t objectBudget = 256ull * 1024 * 1024;
    auto account = [&](const MeshPtr &mesh) {
        meshBytes += (mesh->opaque.size() + mesh->transparent.size() + mesh->water.size()) * sizeof(Vertex) +
                     mesh->opaqueIndices.size() * sizeof(uint32_t);
        if (meshBytes > objectBudget)
            throw std::runtime_error("资产显示网格超过256MiB预算；请减小散布范围或密度");
    };
    auto sample = [&](int x, int y, int z) -> Block {
        if (!w.contains(x, y, z))
            return Air;
        auto c = w.column(x, z);
        auto [it, added] = heights.try_emplace(c);
        if (added)
            it->second = w.height(x, z);
        return w.blockAtHeight(x, y, z, it->second);
    };
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].empty())
            continue;
        auto mesh = std::make_shared<ChunkMesh>();
        mesh->revision = revision;
        std::sort(keys[i].begin(), keys[i].end());
        for (auto key : keys[i]) {
            int x, y, z;
            w.decode(key, x, y, z);
            auto ownNeighbor = [&](int nx, int ny, int nz) -> Block {
                if (!w.contains(nx, ny, nz))
                    return Air;
                const auto neighborKey = w.key(nx, ny, nz);
                const auto owner = w.generated->objectCells.find(neighborKey);
                return owner != w.generated->objectCells.end() && owner->second == i &&
                               !w.overrides.contains(neighborKey)
                           ? sample(nx, ny, nz)
                           : Air;
            };
            // Complete exterior faces remain available while a cached object
            // moves away from terrain or another object. Depth testing hides
            // the contact faces in the committed scene.
            cellMesh(w, *mesh, x, y, z, sample(x, y, z), ownNeighbor);
        }
        bounds(*mesh);
        account(mesh);
        const auto medium = simplify(w, mesh, .35f, .035f);
        if (medium != mesh)
            account(medium);
        const auto distant = simplify(w, mesh, .10f, .09f);
        if (distant != mesh && distant != medium)
            account(distant);
        const auto &object = w.generated->objects[i];
        result.push_back({object.id, object.ownerId, object.placement, mesh, {medium, distant}});
    }
    return result;
}
MeshPtr buildAssetGhost(World w, const StructureAsset &asset) {
    auto key = [](int x, int y, int z) {
        return (uint64_t(uint16_t(x)) << 32) | (uint64_t(uint16_t(y)) << 16) | uint16_t(z);
    };
    std::unordered_map<uint64_t, Block> blocks;
    for (const auto &b : asset.blocks) {
        BlockState state;
        QString error;
        if (parseCreationBlock(b.state, state, error))
            blocks[key(b.x, b.y, b.z)] = w.intern(state);
    }
    auto sample = [&](int x, int y, int z) {
        auto it = blocks.find(key(x, y, z));
        return it == blocks.end() ? Air : it->second;
    };
    auto mesh = std::make_shared<ChunkMesh>();
    for (const auto &b : asset.blocks)
        cellMesh(w, *mesh, b.x, b.y, b.z, sample(b.x, b.y, b.z), sample);
    bounds(*mesh);
    return mesh;
}
} // namespace wb
