#include "TerrainSurface.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wb {
namespace {
struct Point {
    int x, z;
};
struct Tree {
    const SurfaceGrid &g;
    std::vector<float> errors;
    bool boundary;
    int index(Point p) const { return p.z * (g.size + 1) + p.x; }
    bool leaf(Point a, Point c) const { return std::abs(a.x - c.x) + std::abs(a.z - c.z) <= 1; }
    float build(Point a, Point b, Point c) {
        if (leaf(a, c))
            return 0;
        Point m{(a.x + b.x) / 2, (a.z + b.z) / 2};
        const float local = std::abs(g.heights[index(m)] - (g.heights[index(a)] + g.heights[index(b)]) * .5f);
        auto childError = [&](Point p, Point q, Point r) {
            if (leaf(p, r))
                return 0.f;
            return errors[index({(p.x + q.x) / 2, (p.z + q.z) / 2})];
        };
        const float child = std::max(childError(c, a, m), childError(b, c, m));
        // Triangle inequality: |reference-parent| <= |reference-child| +
        // |child-parent|. The second term attains its maximum at m. Merely
        // max(local, child) is NOT a bound against the complete reference.
        float bound = local + child;
        const int width = g.clipWidth ? g.clipWidth : g.size, depth = g.clipDepth ? g.clipDepth : g.size;
        if ((std::min({a.x, b.x, c.x}) < width && std::max({a.x, b.x, c.x}) > width) ||
            (std::min({a.z, b.z, c.z}) < depth && std::max({a.z, b.z, c.z}) > depth))
            bound = std::numeric_limits<float>::infinity();
        if (boundary && (m.x == 0 || m.z == 0 || m.x == g.size || m.z == g.size))
            bound = std::numeric_limits<float>::infinity();
        if (boundary && (m.x == width || m.z == depth))
            bound = std::numeric_limits<float>::infinity();
        // Both sides of a diamond use one split decision, preventing T cracks.
        errors[index(m)] = std::max(errors[index(m)], bound);
        return bound;
    }
    void appendTriangles(Point a, Point b, Point c, float tolerance, SurfaceTopology &out) const {
        if (!leaf(a, c)) {
            Point m{(a.x + b.x) / 2, (a.z + b.z) / 2};
            if (errors[index(m)] > tolerance) {
                appendTriangles(c, a, m, tolerance, out);
                appendTriangles(b, c, m, tolerance, out);
                return;
            }
            out.certifiedError = std::max(out.certifiedError, errors[index(m)]);
        }
        out.indices.insert(out.indices.end(), {uint32_t(index(a)), uint32_t(index(b)), uint32_t(index(c))});
    }
};
} // namespace
SurfaceTopology triangulateSurface(const SurfaceGrid &grid, float tolerance, bool fullBoundary) {
    if (grid.size < 1 || grid.size > 1024 || (grid.size & (grid.size - 1)) ||
        grid.heights.size() != size_t(grid.size + 1) * (grid.size + 1) || !std::isfinite(tolerance) ||
        tolerance < 0 || grid.clipWidth < 0 || grid.clipWidth > grid.size || grid.clipDepth < 0 ||
        grid.clipDepth > grid.size)
        throw std::invalid_argument("Invalid surface grid or tolerance");
    for (float h : grid.heights)
        if (!std::isfinite(h))
            throw std::invalid_argument("Non-finite terrain height");
    Tree tree{grid, std::vector<float>(grid.heights.size(), 0), fullBoundary};
    const Point nw{0, 0}, se{grid.size, grid.size}, ne{grid.size, 0}, sw{0, grid.size};
    // Breadth-first generation followed by bottom-up processing closes diamonds
    // on BOTH sides before any parent reads their error. A recursive root-by-root
    // pass can miss splits introduced later by the other root.
    std::vector<std::array<Point, 3>> nodes{{nw, se, ne}, {se, nw, sw}};
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto [a, b, c] = nodes[i];
        const Point m{(a.x + b.x) / 2, (a.z + b.z) / 2};
        if (!tree.leaf(c, m)) {
            nodes.push_back({c, a, m});
            nodes.push_back({b, c, m});
        }
    }
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        tree.build((*it)[0], (*it)[1], (*it)[2]);
    SurfaceTopology result;
    tree.appendTriangles(nw, se, ne, tolerance, result);
    tree.appendTriangles(se, nw, sw, tolerance, result);
    return result;
}
std::vector<FarTile> planSurfaceTiles(const World &w, int patchSize) {
    if (patchSize < 16 || patchSize > 1024 || (patchSize & (patchSize - 1)))
        throw std::invalid_argument("Invalid terrain patch size");
    std::vector<FarTile> result;
    for (int z = 0; z < w.depth - 1; z += patchSize)
        for (int x = 0; x < w.width - 1; x += patchSize) {
            int width = std::min(patchSize, w.width - 1 - x), depth = std::min(patchSize, w.depth - 1 - z);
            int n = 1;
            while (n < std::max(width, depth))
                n *= 2;
            result.push_back({QRect(w.originX + x, w.originZ + z, width, depth), n, {}});
        }
    return result;
}
MeshPtr buildSurfaceMesh(const World &w, const FarTile &tile, const FarInputs &in, uint64_t revision,
                         float tolerance, const std::atomic_bool *cancelled) {
    const auto r = tile.area;
    const int n = tile.step, side = n + 1;
    SurfaceGrid grid{n, std::vector<float>(size_t(side) * side)};
    grid.clipWidth = r.width();
    grid.clipDepth = r.height();
    auto sourceHeight = [&](int x, int z) {
        return float(w.height(std::clamp(x, w.originX, w.originX + w.width - 1),
                              std::clamp(z, w.originZ, w.originZ + w.depth - 1)) +
                     1);
    };
    // Discard nonintersecting instances in this worker's immutable copy, without
    // changing their ordering or the authoritative world. This is a bound test,
    // never a sparse sampling shortcut.
    auto heights = w.heights;
    std::erase_if(heights.instances, [&](const TerrainInstance &i) {
        return !i.enabled || !i.bounds().intersects(QRectF(r.adjusted(-2, -2, 3, 3)));
    });
    for (int z = 0; z <= n; ++z) {
        if (cancelled && cancelled->load())
            return {};
        for (int x = 0; x <= n; ++x) {
            const int wx = r.x() + std::min(x, r.width()), wz = r.y() + std::min(z, r.height());
            grid.heights[size_t(z) * side + x] = float(heights[w.column(wx, wz)] + 1);
        }
    }
    const auto topology = triangulateSurface(grid, tolerance);
    if (cancelled && cancelled->load())
        return {};
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = revision;
    mesh->surfaceError = topology.certifiedError;
    mesh->sampleCount = uint64_t(r.width() + 1) * (r.height() + 1);
    std::vector<int32_t> ids(grid.heights.size(), -1);
    auto vertex = [&](int x, int z) -> uint32_t {
        x = std::min(x, r.width());
        z = std::min(z, r.height());
        const int index = z * side + x;
        auto &id = ids[index];
        if (id >= 0)
            return uint32_t(id);
        id = int32_t(mesh->opaque.size());
        const int wx = r.x() + x, wz = r.y() + z;
        const float h = grid.heights[index];
        auto at = [&](int xx, int zz) {
            if (xx >= 0 && zz >= 0 && xx <= r.width() && zz <= r.height())
                return grid.heights[zz * side + xx];
            return sourceHeight(r.x() + xx, r.y() + zz);
        };
        auto normal = QVector3D(at(x - 1, z) - at(x + 1, z), 2, at(x, z - 1) - at(x, z + 1)).normalized();
        const auto block = w.blockAtHeight(wx, int(h) - 1, wz, int(h) - 1);
        const auto color = in.colors.at(block);
        mesh->opaque.push_back({wx + .5f, h, wz + .5f, normal.x(), normal.y(), normal.z(), color.x(),
                                color.y(), color.z(), 1, -1, -1});
        return uint32_t(id);
    };
    for (size_t i = 0; i < topology.indices.size(); i += 3) {
        uint32_t a = vertex(topology.indices[i] % side, topology.indices[i] / side);
        uint32_t b = vertex(topology.indices[i + 1] % side, topology.indices[i + 1] / side);
        uint32_t c = vertex(topology.indices[i + 2] % side, topology.indices[i + 2] / side);
        const auto &pa = mesh->opaque[a], &pb = mesh->opaque[b], &pc = mesh->opaque[c];
        if ((pb.x - pa.x) * (pc.z - pa.z) != (pb.z - pa.z) * (pc.x - pa.x))
            mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, b, c});
    }
    // Extend the outermost column centres by half a block, keeping height fixed.
    // There are no full-depth skirts or averaging/downsampling of source heights.
    auto strip = [&](int x0, int z0, int x1, int z1, float dx, float dz) {
        const auto a = vertex(x0, z0), b = vertex(x1, z1);
        auto p = mesh->opaque[a], q = mesh->opaque[b];
        p.x += dx;
        p.z += dz;
        q.x += dx;
        q.z += dz;
        uint32_t c = uint32_t(mesh->opaque.size());
        mesh->opaque.push_back(p);
        mesh->opaque.push_back(q);
        mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, b, c + 1, a, c + 1, c});
    };
    if (r.x() == w.originX)
        for (int z = 0; z < r.height(); ++z)
            strip(0, z, 0, z + 1, -.5f, 0);
    if (r.x() + r.width() == w.originX + w.width - 1)
        for (int z = 0; z < r.height(); ++z)
            strip(r.width(), z, r.width(), z + 1, .5f, 0);
    if (r.y() == w.originZ)
        for (int x = 0; x < r.width(); ++x)
            strip(x, 0, x + 1, 0, 0, -.5f);
    if (r.y() + r.height() == w.originZ + w.depth - 1)
        for (int x = 0; x < r.width(); ++x)
            strip(x, r.height(), x + 1, r.height(), 0, .5f);
    for (int z : {0, r.height()})
        for (int x : {0, r.width()}) {
            int wx = r.x() + x, wz = r.y() + z;
            if ((wx != w.originX && wx != w.originX + w.width - 1) ||
                (wz != w.originZ && wz != w.originZ + w.depth - 1))
                continue;
            const auto base = mesh->opaque[vertex(x, z)];
            const float dx = wx == w.originX ? -.5f : .5f, dz = wz == w.originZ ? -.5f : .5f;
            uint32_t a = uint32_t(mesh->opaque.size());
            for (auto offset : {QPointF(0, 0), QPointF(dx, 0), QPointF(dx, dz), QPointF(0, dz)}) {
                auto p = base;
                p.x += float(offset.x());
                p.z += float(offset.y());
                mesh->opaque.push_back(p);
            }
            mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, a + 1, a + 2, a, a + 2, a + 3});
        }
    mesh->lower = {float(r.x()), float(World::MaxY), float(r.y())};
    mesh->upper = {float(r.x() + r.width() + 1), float(World::MinY), float(r.y() + r.height() + 1)};
    for (const auto &v : mesh->opaque) {
        mesh->lower.setY(std::min(mesh->lower.y(), v.y));
        mesh->upper.setY(std::max(mesh->upper.y(), v.y));
    }
    mesh->center = (mesh->lower + mesh->upper) * .5f;
    return mesh;
}
} // namespace wb
