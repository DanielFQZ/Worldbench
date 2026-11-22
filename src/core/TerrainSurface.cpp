#include "TerrainSurface.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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

// Subtract the READY exact rectangle from the continuous surface. Each clipped
// boundary segment is joined to the adjacent voxel's actual terrain top, rather
// than drawing a world-depth skirt or letting the heightfield cover a cave.
// Both representations are published together by Editor, so there is no frame
// with a cutout but missing chunks. Navigation only invalidates affected patches.
void cutExactArea(ChunkMesh &mesh, const World &w, const QRect &area) {
    mesh.referenceVertices = mesh.opaque.size();
    mesh.surfaceCutout = area;
    if (area.isEmpty())
        return;
    const float left = float(area.x()), right = float(area.x() + area.width());
    const float top = float(area.y()), bottom = float(area.y() + area.height());
    auto interpolate = [](const Vertex &a, const Vertex &b, float t) {
        auto mix = [t](float x, float y) { return x + (y - x) * t; };
        const auto n = QVector3D(mix(a.nx, b.nx), mix(a.ny, b.ny), mix(a.nz, b.nz)).normalized();
        return Vertex{mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), n.x(),
                      n.y(),         n.z(),         mix(a.r, b.r), mix(a.g, b.g),
                      mix(a.b, b.b), mix(a.a, b.a), mix(a.u, b.u), mix(a.v, b.v)};
    };
    auto distance = [&](const Vertex &v, int side) {
        switch (side) {
        case 0:
            return v.x - left;
        case 1:
            return right - v.x;
        case 2:
            return v.z - top;
        default:
            return bottom - v.z;
        }
    };
    auto clip = [&](const std::vector<Vertex> &p, int side, bool inside) {
        std::vector<Vertex> out;
        if (p.empty())
            return out;
        auto a = p.back();
        float da = distance(a, side) * (inside ? 1 : -1);
        for (const auto &b : p) {
            const float db = distance(b, side) * (inside ? 1 : -1);
            if ((da >= 0) != (db >= 0))
                out.push_back(interpolate(a, b, da / (da - db)));
            if (db >= 0)
                out.push_back(b);
            a = b;
            da = db;
        }
        return out;
    };
    auto append = [&](const std::vector<Vertex> &p) {
        if (p.size() < 3)
            return;
        double twiceArea = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            const auto &a = p[i], &b = p[(i + 1) % p.size()];
            twiceArea += double(a.x - p[0].x) * (b.z - p[0].z) - double(b.x - p[0].x) * (a.z - p[0].z);
        }
        if (std::abs(twiceArea) < .000001)
            return;
        const auto first = uint32_t(mesh.opaque.size());
        mesh.opaque.insert(mesh.opaque.end(), p.begin(), p.end());
        for (uint32_t i = 1; i + 1 < p.size(); ++i)
            mesh.opaqueIndices.insert(mesh.opaqueIndices.end(), {first, first + i, first + i + 1});
        for (size_t i = 0; i < p.size(); ++i) {
            auto a = p[i], b = p[(i + 1) % p.size()];
            for (int side = 0; side < 4; ++side) {
                if (std::abs(distance(a, side)) > .0001f || std::abs(distance(b, side)) > .0001f)
                    continue;
                float start = side < 2 ? a.z : a.x, end = side < 2 ? b.z : b.x;
                if (start > end) {
                    std::swap(start, end);
                    std::swap(a, b);
                }
                if (end - start < .0001f)
                    continue;
                float cursor = std::max(start, side < 2 ? top : left),
                      stop = std::min(end, side < 2 ? bottom : right);
                while (cursor < stop - .0001f) {
                    const int column = int(std::floor(cursor + .00001f));
                    const float next = std::min(stop, float(column + 1));
                    const int x = side < 2 ? (side == 0 ? area.left() : area.right()) : column;
                    const int z = side >= 2 ? (side == 2 ? area.top() : area.bottom()) : column;
                    const float height = float(w.height(x, z) + 1);
                    auto p0 = interpolate(a, b, (cursor - start) / (end - start));
                    auto p1 = interpolate(a, b, (next - start) / (end - start));
                    auto p2 = p1, p3 = p0;
                    p2.y = p3.y = height;
                    for (auto *v : {&p0, &p1, &p2, &p3}) {
                        v->nx = side == 0 ? -1.f : side == 1 ? 1.f : 0.f;
                        v->ny = 0;
                        v->nz = side == 2 ? -1.f : side == 3 ? 1.f : 0.f;
                    }
                    const auto k = uint32_t(mesh.opaque.size());
                    mesh.opaque.insert(mesh.opaque.end(), {p0, p1, p2, p3});
                    mesh.opaqueIndices.insert(mesh.opaqueIndices.end(), {k, k + 1, k + 2, k, k + 2, k + 3});
                    cursor = next;
                }
            }
        }
    };
    auto indices = std::move(mesh.opaqueIndices);
    mesh.opaqueIndices.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto a = mesh.opaque[indices[i]], b = mesh.opaque[indices[i + 1]],
                   c = mesh.opaque[indices[i + 2]];
        if (std::max({a.x, b.x, c.x}) < left || std::min({a.x, b.x, c.x}) > right ||
            std::max({a.z, b.z, c.z}) < top || std::min({a.z, b.z, c.z}) > bottom) {
            mesh.opaqueIndices.insert(mesh.opaqueIndices.end(), {indices[i], indices[i + 1], indices[i + 2]});
            continue;
        }
        std::vector<Vertex> remaining{a, b, c};
        for (int side = 0; side < 4 && !remaining.empty(); ++side) {
            append(clip(remaining, side, false));
            remaining = clip(remaining, side, true);
        }
    }
}

// Match authoritative sea-level cells exactly, including explicit air/solid
// overrides. Merge identical row runs instead of emitting two triangles per
// submerged column. The exact region owns its own water faces at every height.
bool appendSeaSurface(ChunkMesh &mesh, const World &w, const FarTile &tile, const SurfaceGrid &grid,
                      const QVector4D &color, const QRect &exactArea, const std::atomic_bool *cancelled) {
    const auto r = tile.area;
    const int width = r.width() + (r.x() + r.width() == w.originX + w.width - 1);
    const int depth = r.height() + (r.y() + r.height() == w.originZ + w.depth - 1);
    const int side = grid.size + 1;
    using Run = std::pair<int, int>;
    std::map<Run, QRect> previous;
    auto append = [&](const QRect &rect) {
        const float y = float(w.seaLevel + 1);
        const float x0 = float(rect.x()), x1 = float(rect.x() + rect.width());
        const float z0 = float(rect.y()), z1 = float(rect.y() + rect.height());
        auto v = [&](float x, float z) {
            return Vertex{x, y, z, 0, 1, 0, color.x(), color.y(), color.z(), .76f, -1, -1};
        };
        mesh.water.insert(mesh.water.end(), {v(x0, z0), v(x1, z1), v(x1, z0),
                                             v(x0, z0), v(x0, z1), v(x1, z1)});
    };
    for (int z = 0; z < depth; ++z) {
        if (cancelled && cancelled->load())
            return false;
        std::map<Run, QRect> current;
        auto wet = [&](int x) {
            const int wx = r.x() + x, wz = r.y() + z;
            if (exactArea.contains(wx, wz))
                return false;
            const int h = int(grid.heights[size_t(z) * side + x]) - 1;
            const auto above = w.blockAtHeight(wx, w.seaLevel + 1, wz, h);
            return w.blockAtHeight(wx, w.seaLevel, wz, h) == Water && above != Water &&
                   (above == Air || w.models[above]->translucent);
        };
        for (int x = 0; x < width;) {
            if (!wet(x)) {
                ++x;
                continue;
            }
            const int start = x++;
            while (x < width && wet(x))
                ++x;
            const Run key{start, x};
            auto found = previous.find(key);
            if (found == previous.end())
                current.emplace(key, QRect(r.x() + start, r.y() + z, x - start, 1));
            else {
                auto rect = found->second;
                rect.setHeight(rect.height() + 1);
                current.emplace(key, rect);
                previous.erase(found);
            }
        }
        for (const auto &[key, rect] : previous)
            append(rect);
        previous = std::move(current);
    }
    for (const auto &[key, rect] : previous)
        append(rect);
    return true;
}
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
MeshPtr buildInteractiveSurfaceMesh(const World &w, const FarTile &tile, const SurfaceMaterials &in,
                                    uint64_t revision, const std::atomic_bool *cancelled, int stride) {
    if (stride < 2 || stride > 32)
        throw std::invalid_argument("Invalid interactive stride");
    const auto r = tile.area;
    auto mesh = std::make_shared<ChunkMesh>();
    mesh->revision = revision;
    mesh->provisional = true;
    auto heights = w.heights;
    std::erase_if(heights.instances, [&](const TerrainInstance &i) {
        return !i.enabled || !i.bounds().intersects(QRectF(r.adjusted(-2, -2, 3, 3)));
    });
    const int side = r.width() + 1;
    std::vector<int32_t> ids(size_t(side) * (r.height() + 1), -1);
    const int cellsX = (r.width() + stride - 1) / stride;
    const int cellsZ = (r.height() + stride - 1) / stride;
    mesh->opaque.reserve((cellsX + 1) * (cellsZ + 1) + cellsX * cellsZ +
                         2 * (r.width() + r.height()));
    mesh->opaqueIndices.reserve(3 * (4 * cellsX * cellsZ + 2 * (r.width() + r.height())));
    const bool left = r.x() == w.originX, right = r.x() + r.width() == w.originX + w.width - 1;
    const bool top = r.y() == w.originZ, bottom = r.y() + r.height() == w.originZ + w.depth - 1;
    auto vertex = [&](int x, int z) {
        auto &id = ids[size_t(z) * side + x];
        if (id >= 0) return uint32_t(id);
        const int wx = r.x() + x, wz = r.y() + z;
        const int h = heights[w.column(wx, wz)];
        const auto color = in.colors.at(w.blockAtHeight(wx, h, wz, h));
        const float px = wx + .5f + (x == 0 && left ? -.5f : x == r.width() && right ? .5f : 0.f);
        const float pz = wz + .5f + (z == 0 && top ? -.5f : z == r.height() && bottom ? .5f : 0.f);
        id = int32_t(mesh->opaque.size());
        mesh->opaque.push_back({px, float(h + 1), pz, 0, 0, 0, color.x(), color.y(), color.z(), 1, -1, -1});
        ++mesh->sampleCount;
        return uint32_t(id);
    };
    auto triangle = [&](uint32_t a, uint32_t b, uint32_t c) {
        mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, b, c});
        const auto &va=mesh->opaque[a], &vb=mesh->opaque[b], &vc=mesh->opaque[c];
        auto n = QVector3D::crossProduct({vb.x-va.x,vb.y-va.y,vb.z-va.z},
                                         {vc.x-va.x,vc.y-va.y,vc.z-va.z});
        if(n.y()<0) n=-n;
        for(auto k:{a,b,c}) {auto &v=mesh->opaque[k];v.nx+=n.x();v.ny+=n.y();v.nz+=n.z();}
    };
    // Each coarse cell is a centre fan. Only cells touching the patch border
    // add per-column edge vertices, matching the full-resolution neighbour.
    for(int z=0;z<r.height();z+=stride) {
        if(cancelled && cancelled->load()) return {};
        const int z1=std::min(z+stride,r.height());
        for(int x=0;x<r.width();x+=stride) {
            const int x1=std::min(x+stride,r.width());
            // At most four edges of the maximum supported stride. Reuse
            // stack storage instead of allocating a vector for every cell.
            std::array<uint32_t, 128> rim;
            size_t rimCount = 0;
            for(int xx=x;xx<x1;xx+=z==0?1:x1-x) rim[rimCount++]=vertex(xx,z);
            for(int zz=z;zz<z1;zz+=x1==r.width()?1:z1-z) rim[rimCount++]=vertex(x1,zz);
            for(int xx=x1;xx>x;xx-=z1==r.height()?1:x1-x) rim[rimCount++]=vertex(xx,z1);
            for(int zz=z1;zz>z;zz-=x==0?1:z1-z) rim[rimCount++]=vertex(x,zz);
            if(x1-x>1 && z1-z>1) {
                const auto center=vertex((x+x1)/2,(z+z1)/2);
                for(size_t i=0;i<rimCount;++i) triangle(center,rim[i],rim[(i+1)%rimCount]);
            } else {
                for(size_t i=1;i+1<rimCount;++i) triangle(rim[0],rim[i],rim[i+1]);
            }
            // Temporary water covers cells touching the sea; opaque terrain
            // depth-clips the shoreline. Final per-column water replaces it.
            bool wet=false;
            for(size_t i=0;i<rimCount;++i) wet |= mesh->opaque[rim[i]].y<=w.seaLevel+1;
            if(wet) {
                const auto c=in.colors.at(Water);
                auto v=[&](uint32_t k){auto p=mesh->opaque[k];p.y=float(w.seaLevel+1);p.nx=0;p.ny=1;p.nz=0;p.r=c.x();p.g=c.y();p.b=c.z();p.a=.76f;return p;};
                auto a=v(vertex(x,z)), b=v(vertex(x1,z)), c0=v(vertex(x1,z1)), d=v(vertex(x,z1));
                mesh->water.insert(mesh->water.end(),{a,b,c0,a,c0,d});
            }
        }
    }
    mesh->lower={float(r.x()),float(World::MaxY),float(r.y())};
    mesh->upper={float(r.x()+r.width()+1),float(World::MinY),float(r.y()+r.height()+1)};
    for(auto &v:mesh->opaque) {
        auto n=QVector3D(v.nx,v.ny,v.nz).normalized();
        if(n.isNull()) n={0,1,0};
        v.nx=n.x();v.ny=n.y();v.nz=n.z();
        mesh->lower.setY(std::min(mesh->lower.y(),v.y));
        mesh->upper.setY(std::max(mesh->upper.y(),v.y));
    }
    if(!mesh->water.empty()) {mesh->lower.setY(std::min(mesh->lower.y(),float(w.seaLevel+1)));mesh->upper.setY(std::max(mesh->upper.y(),float(w.seaLevel+1)));}
    mesh->referenceVertices=mesh->opaque.size();
    mesh->center=(mesh->lower+mesh->upper)*.5f;
    return mesh;
}
MeshPtr buildSurfaceMesh(const World &w, const FarTile &tile, const SurfaceMaterials &in, uint64_t revision,
                         float tolerance, const std::atomic_bool *cancelled, const QRect &exactArea) {
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
    mesh->provisional = tolerance > 1.f;
    mesh->surfaceError = topology.certifiedError;
    mesh->sampleCount = uint64_t(r.width() + 1) * (r.height() + 1);
    // Color is independent of adaptive topology: a one-column painted feature
    // must remain visible even when the terrain there is a single flat triangle.
    const int colorWidth = r.width() + 3, colorHeight = r.height() + 3;
    mesh->surfaceColors = QImage(colorWidth, colorHeight, QImage::Format_RGBA8888);
    if (mesh->surfaceColors.isNull())
        throw std::bad_alloc();
    bool uniformColor = true;
    QColor firstColor;
    for (int z = -1; z <= r.height() + 1; ++z) {
        if (cancelled && cancelled->load())
            return {};
        for (int x = -1; x <= r.width() + 1; ++x) {
            const int wx = std::clamp(r.x() + x, w.originX, w.originX + w.width - 1);
            const int wz = std::clamp(r.y() + z, w.originZ, w.originZ + w.depth - 1);
            const int h = x >= 0 && x <= r.width() && z >= 0 && z <= r.height()
                              ? int(grid.heights[z * side + x]) - 1
                              : int(sourceHeight(wx, wz)) - 1;
            const auto block = w.blockAtHeight(wx, h, wz, h);
            const auto c = in.colors.at(block);
            // Holes/non-heightfield geometry remain a separately labelled R2
            // task. Do not make whole triangles disappear because one texel is air.
            const auto color = QColor::fromRgbF(std::clamp(c.x(), 0.f, 1.f), std::clamp(c.y(), 0.f, 1.f),
                                                std::clamp(c.z(), 0.f, 1.f), 1.f);
            if (x == -1 && z == -1)
                firstColor = color;
            uniformColor = uniformColor && color == firstColor;
            mesh->surfaceColors.setPixelColor(x + 1, z + 1, color);
        }
    }
    // Flat grass/sand regions must not consume four bytes per column over an
    // entire 8K/16K map. A uniform page is represented exactly by one texel.
    if (uniformColor) {
        mesh->surfaceColors = QImage(1, 1, QImage::Format_RGBA8888);
        mesh->surfaceColors.fill(firstColor);
    }
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
        mesh->opaque.push_back({wx + .5f, h, wz + .5f, normal.x(), normal.y(), normal.z(), 1, 1, 1, 1,
                                (x + 1.5f) / colorWidth, (z + 1.5f) / colorHeight});
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
        p.u += dx / colorWidth;
        p.v += dz / colorHeight;
        q.x += dx;
        q.z += dz;
        q.u += dx / colorWidth;
        q.v += dz / colorHeight;
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
                p.u += float(offset.x()) / colorWidth;
                p.v += float(offset.y()) / colorHeight;
                mesh->opaque.push_back(p);
            }
            mesh->opaqueIndices.insert(mesh->opaqueIndices.end(), {a, a + 1, a + 2, a, a + 2, a + 3});
        }
    cutExactArea(*mesh, w, exactArea);
    if (mesh->opaqueIndices.empty()) {
        mesh->opaque.clear();
        mesh->referenceVertices = 0;
    }
    if (!appendSeaSurface(*mesh, w, tile, grid, in.colors.at(Water), exactArea, cancelled))
        return {};
    mesh->lower = {float(r.x()), float(World::MaxY), float(r.y())};
    mesh->upper = {float(r.x() + r.width() + 1), float(World::MinY), float(r.y() + r.height() + 1)};
    for (const auto *vertices : {&mesh->opaque, &mesh->water})
      for (const auto &v : *vertices) {
        mesh->lower.setY(std::min(mesh->lower.y(), v.y));
        mesh->upper.setY(std::max(mesh->upper.y(), v.y));
    }
    mesh->center = (mesh->lower + mesh->upper) * .5f;
    return mesh;
}
} // namespace wb
