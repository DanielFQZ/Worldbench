#include "core/TerrainSurface.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok, const char *text) {
    ++checks;
    if (!ok)
        throw std::runtime_error(text);
}
// Independent reference evaluator: cell diagonal/parity, not RTIN recursion.
double reference(const wb::SurfaceGrid &g, double x, double z) {
    const int side = g.size + 1, ix = std::min(g.size - 1, int(x)), iz = std::min(g.size - 1, int(z));
    const double u = x - ix, v = z - iz, a = g.heights[iz * side + ix], b = g.heights[iz * side + ix + 1],
                 c = g.heights[(iz + 1) * side + ix], d = g.heights[(iz + 1) * side + ix + 1];
    if (((ix + iz) & 1) == 0)
        return v <= u ? a + (b - a) * u + (d - b) * v : a + (d - c) * u + (c - a) * v;
    return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
}
double residual(const wb::SurfaceGrid &g, const wb::SurfaceTopology &mesh, int subsamples = 4) {
    const int n = g.size, side = n + 1;
    std::vector<bool> covered(size_t(n * subsamples + 1) * (n * subsamples + 1));
    double worst = 0, totalArea = 0;
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        for (auto e : {std::pair(a, b), std::pair(b, c), std::pair(c, a)}) {
            if (e.first > e.second)
                std::swap(e.first, e.second);
            ++edges[e];
        }
        const double ax = a % side, az = a / side, bx = b % side, bz = b / side, cx = c % side, cz = c / side;
        const double determinant = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        check(determinant != 0, "nondegenerate triangle");
        totalArea += std::abs(determinant) * .5;
        for (int zz = int(std::min({az, bz, cz}) * subsamples);
             zz <= int(std::max({az, bz, cz}) * subsamples); ++zz)
            for (int xx = int(std::min({ax, bx, cx}) * subsamples);
                 xx <= int(std::max({ax, bx, cx}) * subsamples); ++xx) {
                double x = double(xx) / subsamples, z = double(zz) / subsamples;
                double u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / determinant;
                double v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / determinant;
                if (u < -1e-8 || v < -1e-8 || u + v > 1 + 1e-8)
                    continue;
                double predicted = u * g.heights[a] + v * g.heights[b] + (1 - u - v) * g.heights[c];
                worst = std::max(worst, std::abs(predicted - reference(g, x, z)));
                covered[size_t(zz) * (n * subsamples + 1) + xx] = true;
            }
    }
    check(std::abs(totalArea - n * n) < 1e-6, "complete area with no overlap");
    check(std::all_of(covered.begin(), covered.end(), [](bool b) { return b; }),
          "every independent sample covered");
    for (auto [e, count] : edges) {
        int ax = e.first % side, az = e.first / side, bx = e.second % side, bz = e.second / side;
        const bool boundary = (ax == bx && (ax == 0 || ax == n)) || (az == bz && (az == 0 || az == n));
        check(count == (boundary ? 1 : 2), "conforming mesh has shared internal edges");
    }
    return worst;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QJsonArray reports;
        for (int fixture = 0; fixture < 10; ++fixture) {
            const int n = 32;
            wb::SurfaceGrid g{n, std::vector<float>((n + 1) * (n + 1))};
            for (int z = 0; z <= n; ++z)
                for (int x = 0; x <= n; ++x) {
                    double h = 30;
                    switch (fixture) {
                    case 0:
                        h += x * .5 + z * .25;
                        break;
                    case 1:
                        h += (x == 7 && z == 13) ? 80 : 0;
                        break;
                    case 2:
                        h -= ((x == 21 && z == 9) ? 60 : 0);
                        break;
                    case 3:
                        h += std::min(80., 3 * std::abs(x - 12 - .45 * z));
                        break;
                    case 4:
                        h += 60 * std::exp(-std::pow((x - 8 - .3 * z) / 2, 2));
                        break;
                    case 5:
                        h += x >= 15 ? 100 : 0;
                        break;
                    case 6:
                        h += std::floor(x / 3.) * 4;
                        break;
                    case 7:
                        h += 18 * std::sin(x * .7) * std::cos(z * .31);
                        break;
                    case 8:
                        h += ((x * 92821 + z * 68917 + x * z * 1171) % 97) - 48;
                        break;
                    case 9: {
                        uint32_t value = uint32_t(x * 7987 + z * 3191 + 1);
                        value ^= value << 13;
                        value ^= value >> 17;
                        value ^= value << 5;
                        h += int(value % 137) - 68;
                        break;
                    }
                    }
                    g.heights[z * (n + 1) + x] = float(h);
                }
            for (float tolerance : {0.f, .5f, 1.f, 4.f}) {
                QElapsedTimer timer;
                timer.start();
                auto mesh = wb::triangulateSurface(g, tolerance);
                const double error = residual(g, mesh);
                check(error <= tolerance + 1e-5, "independent residual respects requested tolerance");
                check(error <= mesh.certifiedError + 1e-5, "reported bound contains independent residual");
                check(mesh.certifiedError <= tolerance, "certified bound respects tolerance");
                auto full = wb::triangulateSurface(g, 0);
                check(mesh.indices.size() <= full.indices.size(),
                      "adaptive geometry does not exceed reference");
                wb::SurfaceGrid regular{n / 4, std::vector<float>((n / 4 + 1) * (n / 4 + 1))};
                for (int z = 0; z <= n / 4; ++z)
                    for (int x = 0; x <= n / 4; ++x)
                        regular.heights[z * (n / 4 + 1) + x] = g.heights[z * 4 * (n + 1) + x * 4];
                double regularResidual = 0;
                for (int z = 0; z <= n * 4; ++z)
                    for (int x = 0; x <= n * 4; ++x)
                        regularResidual =
                            std::max(regularResidual, std::abs(reference(g, x / 4., z / 4.) -
                                                               reference(regular, x / 16., z / 16.)));
                if (fixture == 1)
                    check(regularResidual == 80, "coarse regular comparison loses off-grid narrow peak");
                reports.append(QJsonObject{{"fixture", fixture},
                                           {"tolerance", tolerance},
                                           {"residual", error},
                                           {"bound", mesh.certifiedError},
                                           {"triangles", int(mesh.indices.size() / 3)},
                                           {"reference_triangles", 2 * n * n},
                                           {"zero_error_adaptive_triangles", int(full.indices.size() / 3)},
                                           {"regular_step4_triangles", 2 * (n / 4) * (n / 4)},
                                           {"regular_step4_residual", regularResidual},
                                           {"test_ms", double(timer.elapsed())}});
            }
        }
        auto world = wb::World::create({257, 7, -60, 24, 0, true, 193});
        world.heights.restoreDelta(world.heights.activeLayer, world.column(0, 0), 80);
        const auto in = wb::surfaceMaterials(world);
        wb::SurfaceGrid completeReference{256, std::vector<float>(257 * 257)};
        for (int z = 0; z <= 256; ++z)
            for (int x = 0; x <= 256; ++x)
                completeReference.heights[z * 257 + x] =
                    float(world.height(world.originX + x, world.originZ + std::min(z, world.depth - 1)) + 1);
        auto tiles = wb::planSurfaceTiles(world, 128);
        std::map<std::pair<float, float>, float> positions;
        size_t reused = 0;
        double area = 0;
        for (const auto &tile : tiles) {
            auto mesh = wb::buildSurfaceMesh(world, tile, in, 1);
            for (auto p : mesh->opaque) {
                auto [it, inserted] = positions.emplace(std::pair(p.x, p.z), p.y);
                if (!inserted) {
                    ++reused;
                    check(it->second == p.y, "shared patch samples are identical");
                }
                check(p.x >= world.originX && p.x <= world.originX + world.width && p.z >= world.originZ &&
                          p.z <= world.originZ + world.depth,
                      "rectangular geometry remains in map");
            }
            for (size_t i = 0; i < mesh->opaqueIndices.size(); i += 3) {
                auto a = mesh->opaque[mesh->opaqueIndices[i]], b = mesh->opaque[mesh->opaqueIndices[i + 1]],
                     c = mesh->opaque[mesh->opaqueIndices[i + 2]];
                double d = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
                check(d != 0, "cropped triangles have nonzero area");
                area += std::abs(d) * .5;
                // Independent quarter-cell checks on the actual world-space
                // mesh include the cropped rectangular patch, not just topology.
                int x0 = std::max(0, int(std::ceil((std::min({a.x, b.x, c.x}) - world.originX - .5f) * 2)));
                int x1 = std::min((world.width - 1) * 2,
                                  int(std::floor((std::max({a.x, b.x, c.x}) - world.originX - .5f) * 2)));
                int z0 = std::max(0, int(std::ceil((std::min({a.z, b.z, c.z}) - world.originZ - .5f) * 2)));
                int z1 = std::min((world.depth - 1) * 2,
                                  int(std::floor((std::max({a.z, b.z, c.z}) - world.originZ - .5f) * 2)));
                for (int iz = z0; iz <= z1; ++iz)
                    for (int ix = x0; ix <= x1; ++ix) {
                        double x = world.originX + .5 + ix * .5, z = world.originZ + .5 + iz * .5;
                        double u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
                        double v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
                        if (u < -1e-8 || v < -1e-8 || u + v > 1 + 1e-8)
                            continue;
                        const double height = u * a.y + v * b.y + (1 - u - v) * c.y;
                        check(std::abs(height - reference(completeReference, ix * .5, iz * .5)) <= .50001,
                              "rectangular world-space residual respects reference");
                    }
            }
        }
        check(reused > 128, "patch seam has complete shared samples");
        check(area == double(world.width) * world.depth,
              "rectangular map and half-cell border covered exactly");
        check(positions.at({.5f, .5f}) == 105, "one-column spike remains at column centre");
        // Independently measure the XZ coverage after local voxel replacement.
        // Vertical transition faces contribute zero projected area. The two
        // representations must neither overlap inside the cutout nor leave holes.
        const QRect worldArea(world.originX, world.originZ, world.width, world.depth);
        for (const QRect cutout : {QRect(-32, -16, 96, 64), QRect(-17, -33, 144, 144),
                                   QRect(world.originX, world.originZ, 144, 144), worldArea}) {
            double visibleArea = 0;
            for (const auto &tile : tiles) {
                const auto clipped = wb::buildSurfaceMesh(world, tile, in, 7, 1.f, nullptr, cutout);
                for (size_t i = 0; i < clipped->opaqueIndices.size(); i += 3) {
                    const auto a = clipped->opaque[clipped->opaqueIndices[i]],
                               b = clipped->opaque[clipped->opaqueIndices[i + 1]],
                               c = clipped->opaque[clipped->opaqueIndices[i + 2]];
                    const double area2 = double(b.x - a.x) * (c.z - a.z) - double(b.z - a.z) * (c.x - a.x);
                    visibleArea += std::abs(area2) * .5;
                    if (std::abs(area2) > .000001) {
                        const double x = (double(a.x) + b.x + c.x) / 3, z = (double(a.z) + b.z + c.z) / 3;
                        check(!(x > cutout.x() + .0001 && x < cutout.x() + cutout.width() - .0001 &&
                                z > cutout.y() + .0001 && z < cutout.y() + cutout.height() - .0001),
                              "continuous surface never covers ready voxel/cave region");
                    } else
                        check(std::min({a.y, b.y, c.y}) >= 25,
                              "transition joins surface tops without world-depth skirts");
                }
                if (cutout == worldArea)
                    check(clipped->opaque.empty() && clipped->opaqueIndices.empty(),
                          "fully replaced surface has no unindexed fallback geometry");
            }
            const auto removed = cutout.intersected(worldArea);
            check(std::abs(visibleArea - (double(world.width) * world.depth -
                                          double(removed.width()) * removed.height())) < .05,
                  "clipped reference surface plus exact rectangle covers map without overlap or gaps");
        }
        std::atomic_bool cancelled{true};
        check(!wb::buildSurfaceMesh(world, tiles.front(), in, 3, .5, &cancelled),
              "cancelled build publishes no partial mesh");
        auto bad = wb::SurfaceGrid{3, std::vector<float>(16)};
        bool rejected = false;
        try {
            wb::triangulateSurface(bad, .5);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "reject non-power-of-two topology");
        // Top-face atlas colors, including high-frequency resource-pack patterns,
        // are sampled independently of terrain tessellation and vertex density.
        auto colored = wb::World::create({65, 7, -60, 24, 0, true, 65});
        auto assets = std::make_shared<wb::AssetLibrary>();
        assets->atlas = QImage(32, 32, QImage::Format_RGBA8888);
        for (int z = 0; z < 32; ++z)
            for (int x = 0; x < 32; ++x)
                assets->atlas.setPixelColor(x, z, x % 8 == 4 ? Qt::red : Qt::blue);
        colored.assets = assets;
        auto model = std::make_shared<wb::BlockModel>();
        wb::ModelFace face;
        face.normal = {0, 1, 0};
        face.uv[0] = {0, 0};
        face.uv[1] = {1, 0};
        face.uv[2] = {1, 1};
        face.uv[3] = {0, 1};
        model->faces.push_back(face);
        colored.models[wb::Grass] = model;
        const auto colors = wb::surfaceMaterials(colored);
        check(std::abs(colors.colors[wb::Grass].x() - .125f) < 1e-6 &&
                  std::abs(colors.colors[wb::Grass].z() - .875f) < 1e-6,
              "all texture samples contribute without scanning sparse edits");
        const auto tile = wb::planSurfaceTiles(colored).front();
        auto beforeColor = wb::buildSurfaceMesh(colored, tile, colors, 1);
        check(beforeColor->surfaceColors.size() == QSize(1, 1) && beforeColor->surfaceColorBytes() == 4,
              "uniform surface colors use one exact texel rather than a full-map RGBA allocation");
        const auto grassColor = beforeColor->surfaceColors.pixelColor(0, 0);
        colored.setBlock(2, 24, 4, wb::Stone);
        auto afterColor = wb::buildSurfaceMesh(colored, tile, colors, 2);
        check(beforeColor->opaqueIndices == afterColor->opaqueIndices &&
                  beforeColor->opaque.size() == afterColor->opaque.size(),
              "a material edit does not change geometry");
        check(afterColor->surfaceColors.pixelColor(35, 37) != grassColor &&
                  afterColor->surfaceColors.pixelColor(34, 37) == grassColor,
              "one-column material edits survive sparse adaptive geometry");
        for (const auto &v : afterColor->opaque)
            check(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1 && v.r == 1 && v.g == 1 && v.b == 1,
                  "surface page coordinates cover geometry without applying tint twice");
        // Independently count sea-level cell coverage across odd rectangular
        // patch boundaries and the exact-region cutout. No cell may be repeated.
        auto lake = wb::World::create({97, 42, 12, 0, 0, true, 65});
        const QRect exact(-16, -16, 32, 32);
        lake.setBlock(25, 12, 10, wb::Air);
        lake.setBlock(26, 12, 10, wb::Stone);
        lake.setBlock(27, 13, 10, wb::Stone);
        lake.heights.set(lake.column(28, 10), 24);
        std::map<std::pair<int, int>, int> waterCoverage;
        auto accumulate = [&](const wb::MeshPtr &mesh) {
            check(mesh->water.size() % 6 == 0, "water consists of complete quads");
            for (size_t i = 0; i < mesh->water.size(); i += 6) {
                const auto &a = mesh->water[i];
                if (a.ny < .9f || a.y != 13) continue;
                float x0=a.x, x1=a.x, z0=a.z, z1=a.z;
                for(size_t k=i;k<i+6;++k) {
                    const auto &p=mesh->water[k];
                    x0=std::min(x0,p.x); x1=std::max(x1,p.x); z0=std::min(z0,p.z); z1=std::max(z1,p.z);
                }
                for(int z=int(z0);z<int(z1);++z)for(int x=int(x0);x<int(x1);++x)++waterCoverage[{x,z}];
            }
        };
        const auto waterColors=wb::surfaceMaterials(lake);
        for (const auto &tile : wb::planSurfaceTiles(lake,32))
            accumulate(wb::buildSurfaceMesh(lake,tile,waterColors,1,1,nullptr,exact));
        for(int z=-1;z<=0;++z)for(int x=-1;x<=0;++x)accumulate(wb::buildChunkMesh(lake,x,z,1));
        for(int z=lake.originZ;z<lake.originZ+lake.depth;++z)for(int x=lake.originX;x<lake.originX+lake.width;++x) {
            const auto above=lake.block(x,13,z);
            const bool wet=lake.block(x,12,z)==wb::Water && (above==wb::Air||lake.models[above]->translucent) && above!=wb::Water;
            check(waterCoverage[{x,z}]==int(wet),"exactly one water top per authoritative wet cell, including boundaries and air overrides");
        }
        auto ocean=wb::World::create({16384,42,12,0,0,true,8192});
        const auto oceanTile=wb::planSurfaceTiles(ocean,256).front();
        const auto oceanMesh=wb::buildSurfaceMesh(ocean,oceanTile,wb::surfaceMaterials(ocean),1);
        check(oceanMesh->water.size()==6,"flat open water patch merges to two triangles, not per-cell quads");
        wb::Environment original, loaded;
        auto legacy=wb::saveEnvironment(original);
        for(const char *key:{"water","waterWaves","waterReflections","shadows","waveStrength","waveDensity","waveSpeed"})legacy.remove(key);
        QString envError;
        check(wb::loadEnvironment(legacy,loaded,envError)&&loaded==original,"old v9 environment gets compatible water/shadow defaults");
        wb::Environment updated;
        check(wb::patchEnvironment(original,{{"water",false},{"waterWaves",false},{"waterReflections",false},{"shadows",false},{"waveStrength",80},{"waveDensity",72},{"waveSpeed",18}},updated,envError),"new effect controls validate");
        check(wb::loadEnvironment(wb::saveEnvironment(updated),loaded,envError)&&loaded==updated,"all effect controls serialize");
        check(!wb::patchEnvironment(original,{{"waveStrength",101}},updated,envError)&&!wb::patchEnvironment(original,{{"waveDensity",0}},updated,envError)&&!wb::patchEnvironment(original,{{"shadows",1}},updated,envError),"effect range and type errors rejected");
        std::cout << QJsonDocument(reports).toJson().constData() << "\nSurface checks: " << checks
                  << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
