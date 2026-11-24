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
void simplifyPart(const World &w, std::vector<Vertex> &v, std::vector<uint32_t> &outputIndices, float ratio,
                  float error, float &surfaceError) {
    if (v.empty())
        return;
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
    outputIndices.resize(indices.size());
    float achieved = 0;
    size_t count = meshopt_simplify(
        outputIndices.data(), indices.data(), indices.size(), &v[0].x, v.size(), sizeof(Vertex),
        std::max(size_t(36), size_t(indices.size() * ratio) / 3 * 3), error, 0, &achieved);
    if (count < 3) {
        outputIndices = indices;
        count = indices.size();
        achieved = 0;
    }
    outputIndices.resize(count);
    surfaceError =
        std::max(surfaceError, achieved * meshopt_simplifyScale(&v[0].x, v.size(), sizeof(Vertex)));
    // Accumulate smooth normals only for the reduced representation.
    for (auto &vertex : v)
        vertex.nx = vertex.ny = vertex.nz = 0;
    for (size_t i = 0; i < count; i += 3) {
        auto &a = v[outputIndices[i]], &b = v[outputIndices[i + 1]], &c = v[outputIndices[i + 2]];
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
    size_t used = meshopt_optimizeVertexFetch(compact.data(), outputIndices.data(), count, v.data(), v.size(),
                                              sizeof(Vertex));
    compact.resize(used);
    v = std::move(compact);
}

MeshPtr simplify(const World &w, const MeshPtr &source, float ratio, float error) {
    auto mesh = std::make_shared<ChunkMesh>(*source);
    simplifyPart(w, mesh->opaque, mesh->opaqueIndices, ratio, error, mesh->surfaceError);
    // Alpha and water passes take triangle lists. Keep their material class
    // while reducing their geometry under the same LOD rule as opaque blocks.
    for (auto *part : {&mesh->transparent, &mesh->water}) {
        if (part->empty())
            continue;
        std::vector<uint32_t> indices;
        simplifyPart(w, *part, indices, ratio, error, mesh->surfaceError);
        std::vector<Vertex> triangles;
        triangles.reserve(indices.size());
        for (uint32_t i : indices)
            triangles.push_back((*part)[i]);
        *part = std::move(triangles);
    }
    return mesh;
}
// Subsequent opaque levels decimate the welded, tinted mesh from the previous
// level. No atlas lookup or seam welding is repeated. Error is in world units.
MeshPtr nextLevel(const MeshPtr &previous, float ratio, float tolerance) {
    auto &src = previous->opaque;
    auto &indices = previous->opaqueIndices;
    if (indices.size() <= 24)
        return previous;
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->lower = previous->lower;
    mesh->upper = previous->upper;
    mesh->center = previous->center;
    mesh->revision = previous->revision;
    mesh->opaqueIndices.resize(indices.size());
    float error = 0;
    auto count = meshopt_simplify(
        mesh->opaqueIndices.data(), indices.data(), indices.size(), &src[0].x, src.size(), sizeof(Vertex),
        std::max(size_t(24), size_t(indices.size() * ratio) / 3 * 3), tolerance, 0, &error);
    if (count < 3 || count >= indices.size())
        return previous;
    mesh->opaqueIndices.resize(count);
    mesh->opaque.resize(src.size());
    auto used = meshopt_optimizeVertexFetch(mesh->opaque.data(), mesh->opaqueIndices.data(), count,
                                            src.data(), src.size(), sizeof(Vertex));
    mesh->opaque.resize(used);
    mesh->opaque.shrink_to_fit();
    mesh->opaqueIndices.shrink_to_fit();
    mesh->surfaceError =
        previous->surfaceError + error * meshopt_simplifyScale(&src[0].x, src.size(), sizeof(Vertex));
    return mesh;
}
} // namespace

std::vector<MeshScene::Object> buildCreationMeshes(const World &w, uint64_t revision,
                                                   std::vector<MeshScene::Cluster> *clusters,
                                                   const MeshScene *previousScene) {
    std::vector<MeshScene::Object> result;
    if (!w.generated)
        return result;
    std::vector<std::vector<uint64_t>> keys(w.generated->objects.size());
    for (auto [key, owner] : w.generated->objectCells)
        if (owner < keys.size() && !w.explicitOverride(key))
            keys[owner].push_back(key);
    std::unordered_map<int, int> heights;
    size_t meshBytes = 0;
    constexpr size_t objectBudget = 256ull * 1024 * 1024;
    auto bytes = [](const MeshPtr &mesh) {
        return (mesh->opaque.size() + mesh->transparent.size() + mesh->water.size()) * sizeof(Vertex) +
               mesh->opaqueIndices.size() * sizeof(uint32_t);
    };
    auto account = [&](const MeshPtr &mesh) {
        meshBytes += bytes(mesh);
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
    QHash<QString, const MeshScene::Object *> cached;
    if (previousScene && previousScene->assets == w.assets &&
        previousScene->worldArea == QRect(w.originX, w.originZ, w.width, w.depth))
        for (const auto &object : previousScene->objects)
            cached.insert(object.id, &object);
    std::map<size_t, const MeshScene::Object *> reused;
    auto translate = [](MeshPtr src, QVector3D offset) -> MeshPtr {
        if (offset.isNull())
            return src;
        auto mesh = std::make_shared<ChunkMesh>(*src);
        for (auto *part : {&mesh->opaque, &mesh->transparent, &mesh->water})
            for (auto &v : *part) {
                v.x += offset.x();
                v.y += offset.y();
                v.z += offset.z();
            }
        mesh->center += offset;
        mesh->lower += offset;
        mesh->upper += offset;
        return mesh;
    };
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].empty())
            continue;
        auto mesh = std::make_shared<ChunkMesh>();
        mesh->revision = revision;
        std::sort(keys[i].begin(), keys[i].end());
        const auto &object = w.generated->objects[i];
        const auto root = object.placement.position;
        const int64_t origin =
            (int64_t(qRound(root.y())) * w.depth + qRound(root.z())) * w.width + qRound(root.x());
        auto shape = std::make_shared<std::vector<std::pair<int64_t, Block>>>();
        shape->reserve(keys[i].size());
        for (auto key : keys[i])
            shape->emplace_back(int64_t(key) - origin, w.generated->cells.at(key));
        const auto *old = cached.value(object.id);
        if (old && old->shape && *shape == *old->shape &&
            old->placement.assetId == object.placement.assetId) {
            auto entry = *old;
            entry.ownerId = object.ownerId;
            entry.placement = object.placement;
            auto delta = root - old->placement.position;
            if (delta == QVector3D(qRound(delta.x()), qRound(delta.y()), qRound(delta.z()))) {
                entry.mesh = translate(entry.mesh, delta);
                account(entry.mesh);
                entry.lods.resize(1);
                entry.lods.front() = translate(entry.lods.front(), delta);
                account(entry.lods.front());
                reused.emplace(result.size(), old);
                result.push_back(std::move(entry));
                continue;
            }
        }
        for (auto key : keys[i]) {
            int x, y, z;
            w.decode(key, x, y, z);
            auto ownNeighbor = [&](int nx, int ny, int nz) -> Block {
                if (!w.contains(nx, ny, nz))
                    return Air;
                const auto neighborKey = w.key(nx, ny, nz);
                const auto owner = w.generated->objectCells.find(neighborKey);
                return owner != w.generated->objectCells.end() && owner->second == i &&
                               !w.explicitOverride(neighborKey)
                           ? sample(nx, ny, nz)
                           : Air;
            };
            // Complete exterior faces remain available while a cached object
            // moves away from terrain or another object. Depth testing hides
            // the contact faces in the committed scene.
            cellMesh(w, *mesh, x, y, z, sample(x, y, z), ownNeighbor);
        }
        if (mesh->opaque.empty() && mesh->transparent.empty() && mesh->water.empty())
            continue;
        bounds(*mesh);
        account(mesh);
        MeshScene::Object entry{object.id, object.ownerId, object.placement, mesh, {}, std::move(shape)};
        auto first = simplify(w, mesh, .28f, .027f);
        account(first);
        entry.lods.push_back(std::move(first));
        result.push_back(std::move(entry));
    }
    // Reserve the original mesh and first distant mesh for every object before
    // spending the remaining preview budget on optional finer LOD transitions.
    constexpr std::array<float, 5> ratios{.28f, .14f, .07f, .032f, .012f};
    constexpr std::array<float, 5> tolerances{.027f, .05f, .10f, .20f, .42f};
    for (size_t i = 0; i < result.size(); ++i) {
        auto &entry = result[i];
        if (auto found = reused.find(i); found != reused.end()) {
            const auto &old = *found->second;
            const auto delta = entry.placement.position - old.placement.position;
            for (size_t level = 1; level < old.lods.size(); ++level) {
                if (bytes(old.lods[level]) > objectBudget - meshBytes)
                    continue;
                auto lod = translate(old.lods[level], delta);
                account(lod);
                entry.lods.push_back(std::move(lod));
            }
            continue;
        }
        size_t previousLevel = 0;
        for (size_t level = 1; level < ratios.size(); ++level) {
            const auto &previous = entry.lods.back();
            auto lod = entry.mesh->transparent.empty() && entry.mesh->water.empty()
                           ? nextLevel(previous, ratios[level] / ratios[previousLevel], tolerances[level])
                           : simplify(w, entry.mesh, ratios[level], tolerances[level]);
            if (lod == previous || bytes(lod) >= bytes(previous))
                continue;
            if (bytes(lod) > objectBudget - meshBytes)
                continue;
            account(lod);
            entry.lods.push_back(std::move(lod));
            previousLevel = level;
        }
    }
    if (clusters) {
        // One far draw per occupied cell of a PCG volume. Do not merge
        // independently selectable placements or transparent sorting domains.
        std::map<std::tuple<QString, int, int>, std::vector<size_t>> groups;
        for (size_t i = 0; i < result.size(); ++i) {
            const auto &o = result[i];
            if (o.id == o.ownerId || o.lods.empty() || !o.mesh->transparent.empty() || !o.mesh->water.empty())
                continue;
            groups[{o.ownerId, int(std::floor(o.mesh->center.x() / 64)),
                    int(std::floor(o.mesh->center.z() / 64))}]
                .push_back(i);
        }
        for (auto &[key, members] : groups) {
            if (members.size() < 2)
                continue;
            // Prefer the cheapest acceptable level at draw time. The middle
            // band also batches forests whose outer silhouette is still visible.
            for (size_t level : {size_t(4), size_t(1)}) {
                size_t batchBytes = 0;
                for (auto i : members) {
                    const auto &levels = result[i].lods;
                    batchBytes += bytes(levels[std::min(level, levels.size() - 1)]);
                }
                if (batchBytes > objectBudget - meshBytes)
                    continue;
                auto batch = std::make_shared<ChunkMesh>();
                batch->revision = revision;
                batch->lower = result[members.front()].mesh->lower;
                batch->upper = result[members.front()].mesh->upper;
                for (auto i : members) {
                    const auto &o = result[i];
                    auto &src = *o.lods[std::min(level, o.lods.size() - 1)];
                    uint32_t offset = uint32_t(batch->opaque.size());
                    batch->opaque.insert(batch->opaque.end(), src.opaque.begin(), src.opaque.end());
                    for (auto idx : src.opaqueIndices)
                        batch->opaqueIndices.push_back(offset + idx);
                    for (int axis = 0; axis < 3; ++axis) {
                        batch->lower[axis] = std::min(batch->lower[axis], o.mesh->lower[axis]);
                        batch->upper[axis] = std::max(batch->upper[axis], o.mesh->upper[axis]);
                    }
                    batch->surfaceError = std::max(batch->surfaceError, src.surfaceError);
                }
                batch->center = (batch->lower + batch->upper) * .5f;
                account(batch);
                clusters->push_back({batch, members});
            }
        }
    }
    return result;
}
MeshPtr buildTerrainAssetPreview(const TerrainInstance &terrain, int maxSamples) {
    auto mesh = std::make_shared<ChunkMesh>();
    const auto &image = *terrain.image;
    const int nx = std::clamp(image.width, 2, maxSamples), nz = std::clamp(image.depth, 2, maxSamples);
    const float sx = std::max(1, terrain.width - 1) * terrain.scaleX * .01f;
    const float sy = (terrain.high - terrain.low) * terrain.scaleY * .01f;
    const float sz = std::max(1, terrain.depth - 1) * terrain.scaleZ * .01f;
    QMatrix4x4 pose;
    pose.scale(64.f / std::max({sx, sy, sz}));
    pose.rotate(terrain.rotationY, 0, 1, 0);
    pose.rotate(terrain.rotationX, 1, 0, 0);
    pose.rotate(terrain.rotationZ, 0, 0, 1);
    std::vector<QVector3D> points;
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            float u = float(x) / (nx - 1), v = float(z) / (nz - 1);
            points.push_back(
                pose.map(QVector3D((u - .5f) * sx, float(image.sample(u, v) - .5) * sy, (v - .5f) * sz)));
        }
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            int i = z * nx + x;
            auto dx = points[z * nx + std::min(nx - 1, x + 1)] - points[z * nx + std::max(0, x - 1)];
            auto dz = points[std::min(nz - 1, z + 1) * nx + x] - points[std::max(0, z - 1) * nx + x];
            auto n = QVector3D::crossProduct(dz, dx).normalized(), p = points[i];
            mesh->opaque.push_back(
                {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), .62f, .71f, .53f, 1.f, -1.f, -1.f});
            if (x + 1 < nx && z + 1 < nz) {
                uint32_t a = i, b = i + nx;
                mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        }
    bounds(*mesh);
    return mesh;
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
