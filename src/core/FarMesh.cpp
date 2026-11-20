#include "FarMesh.h"
#include <QMatrix4x4>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace wb {
std::vector<FarTile> planFarTiles(const World &w, const std::set<int> &exact, int fx, int fz) {
    std::vector<FarTile> result;
    if (w.width <= 256 && w.depth <= 256)
        return result;
    int size = 16;
    while (size < std::max(w.width, w.depth))
        size *= 2;
    QRect near;
    for (int id : exact) {
        QRect chunk(id % w.chunksX() * 16, id / w.chunksX() * 16, 16, 16);
        near = near.isNull() ? chunk : near.united(chunk);
    }
    const QRect bounds(0, 0, w.width, w.depth);
    fx -= w.originX;
    fz -= w.originZ;
    std::function<void(int, int, int)> visit = [&](int x, int z, int s) {
        const QRect r = QRect(x, z, s, s).intersected(bounds);
        if (r.isEmpty())
            return;
        if (s == 16 && exact.contains(z / 16 * w.chunksX() + x / 16))
            return;
        const int distance = std::max({x - fx, fx - (x + s), z - fz, fz - (z + s), 0});
        if (s > 16 && (r.intersects(near) || distance < s * 2)) {
            const int h = s / 2;
            visit(x, z, h);
            visit(x + h, z, h);
            visit(x, z + h, h);
            visit(x + h, z + h, h);
        } else
            result.push_back({r.translated(w.originX, w.originZ), s / 16, {}});
    };
    visit(0, 0, size);
    return result;
}
FarInputs farInputs(const World &w) {
    FarInputs result;
    for (const auto &l : w.heights.layers)
        if (l.enabled)
            for (auto [index, delta] : *l.deltas)
                result.columns[index / w.width + w.originZ].try_emplace(index % w.width + w.originX,
                                                                        World::MinY);
    for (auto [key, block] : w.overrides) {
        int x, y, z;
        w.decode(key, x, y, z);
        auto [it, inserted] = result.columns[z].try_emplace(x, World::MinY);
        if (!w.isAir(block))
            it->second = std::max(it->second, y);
    }
    // An area average avoids texture aliasing on large voxel cells. These colors
    // are derived from the current model/atlas, so resource pack changes apply.
    for (const auto &model : w.models) {
        QVector4D color(.5f, .5f, .5f, 1);
        const ModelFace *face = nullptr;
        for (const auto &f : model->faces)
            if (!face || f.normal.y() > face->normal.y())
                face = &f;
        if (face) {
            color = face->color;
            if (w.assets) {
                const auto &atlas = w.assets->atlas;
                QVector4D sum;
                float weight = 0;
                for (int z = 0; z < 4; ++z)
                    for (int x = 0; x < 4; ++x) {
                        auto uv = face->uv[0] + (face->uv[1] - face->uv[0]) * ((x + .5f) / 4) +
                                  (face->uv[3] - face->uv[0]) * ((z + .5f) / 4);
                        auto p =
                            atlas.pixelColor(std::clamp(int(uv.x() * atlas.width()), 0, atlas.width() - 1),
                                             std::clamp(int(uv.y() * atlas.height()), 0, atlas.height() - 1));
                        sum += QVector4D(p.redF(), p.greenF(), p.blueF(), 1) * p.alphaF();
                        weight += p.alphaF();
                    }
                if (weight > 0)
                    color *= sum / weight;
            }
        }
        result.colors.push_back(color);
    }
    return result;
}
MeshPtr buildFarMesh(const World &w, const FarTile &tile, const FarInputs &in, uint64_t revision) {
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = revision;
    const auto r = tile.area;
    const int step = tile.step, nx = (r.width() + step - 1) / step, nz = (r.height() + step - 1) / step;
    struct Column {
        int y = World::MinY;
        Block block = Bedrock;
    };
    std::vector<Column> cells(nx * nz);
    auto sample = [&](Column &cell, int x, int z, int explicitTop = World::MinY) {
        const int h = w.height(x, z);
        int y = std::max({h, w.seaLevel, explicitTop});
        Block b = w.blockAtHeight(x, y, z, h);
        while (w.isAir(b) && y > World::MinY)
            b = w.blockAtHeight(x, --y, z, h);
        if (y >= cell.y)
            cell = {y, b};
    };
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            auto &c = cells[z * nx + x];
            const int x0 = r.x() + x * step, z0 = r.y() + z * step;
            const int x1 = std::min(x0 + step - 1, r.right()), z1 = std::min(z0 + step - 1, r.bottom());
            sample(c, (x0 + x1) / 2, (z0 + z1) / 2);
            if (step > 1) {
                sample(c, x0, z0);
                sample(c, x1, z0);
                sample(c, x0, z1);
                sample(c, x1, z1);
            }
        }
    for (auto row = in.columns.lower_bound(r.top()); row != in.columns.end() && row->first <= r.bottom();
         ++row)
        for (auto col = row->second.lower_bound(r.left());
             col != row->second.end() && col->first <= r.right(); ++col)
            sample(cells[(row->first - r.y()) / step * nx + (col->first - r.x()) / step], col->first,
                   row->first, col->second);
    const int indices[] = {0, 1, 2, 0, 2, 3};
    auto quad = [&](std::array<QVector3D, 4> p, QVector3D n, Block block) {
        auto color = in.colors[block];
        // Far summaries represent the top envelope, including water. Exact
        // transparency, thin models and underground voids resolve in near chunks.
        for (int i : indices)
            mesh->opaque.push_back({p[i].x(), p[i].y(), p[i].z(), n.x(), n.y(), n.z(), color.x(), color.y(),
                                    color.z(), 1, -1, -1});
    };
    int maxY = World::MinY;
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            const auto &c = cells[z * nx + x];
            const float x0 = float(r.x() + x * step), z0 = float(r.y() + z * step);
            const float x1 = float(std::min(r.x() + (x + 1) * step, r.right() + 1));
            const float z1 = float(std::min(r.y() + (z + 1) * step, r.bottom() + 1));
            const float top = float(c.y + 1);
            maxY = std::max(maxY, c.y + 1);
            quad({QVector3D{x0, top, z0}, {x1, top, z0}, {x1, top, z1}, {x0, top, z1}}, {0, 1, 0}, c.block);
            // Full-height boundary skirts close coarse/fine interfaces without
            // covering any neighbouring top face; all surfaces remain axis aligned.
            const float left = float(x ? cells[z * nx + x - 1].y + 1 : World::MinY);
            const float right = float(x + 1 < nx ? cells[z * nx + x + 1].y + 1 : World::MinY);
            const float back = float(z ? cells[(z - 1) * nx + x].y + 1 : World::MinY);
            const float front = float(z + 1 < nz ? cells[(z + 1) * nx + x].y + 1 : World::MinY);
            if (top > left)
                quad({QVector3D{x0, left, z0}, {x0, top, z0}, {x0, top, z1}, {x0, left, z1}}, {-1, 0, 0},
                     c.block);
            if (top > right)
                quad({QVector3D{x1, right, z0}, {x1, top, z0}, {x1, top, z1}, {x1, right, z1}}, {1, 0, 0},
                     c.block);
            if (top > back)
                quad({QVector3D{x0, back, z0}, {x0, top, z0}, {x1, top, z0}, {x1, back, z0}}, {0, 0, -1},
                     c.block);
            if (top > front)
                quad({QVector3D{x0, front, z1}, {x0, top, z1}, {x1, top, z1}, {x1, front, z1}}, {0, 0, 1},
                     c.block);
        }
    mesh->lower = {float(r.left()), float(World::MinY), float(r.top())};
    mesh->upper = {float(r.right() + 1), float(maxY), float(r.bottom() + 1)};
    mesh->center = (mesh->lower + mesh->upper) * .5f;
    return mesh;
}
bool visibleMesh(const ChunkMesh &m, const QMatrix4x4 &matrix) {
    // Canonical OpenGL clip space, before the backend correction matrix.
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            QVector4D plane = matrix.row(3) + matrix.row(axis) * float(sign);
            QVector3D p(plane.x() >= 0 ? m.upper.x() : m.lower.x(),
                        plane.y() >= 0 ? m.upper.y() : m.lower.y(),
                        plane.z() >= 0 ? m.upper.z() : m.lower.z());
            if (QVector3D::dotProduct(plane.toVector3D(), p) + plane.w() < 0)
                return false;
        }
    return true;
}
} // namespace wb
