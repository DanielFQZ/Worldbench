#include "core/Project.h"
#include "core/RegionEdit.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    };
    try {
        auto world = wb::World::create(wb::WorldOptions{96, 42, -60, 24, 20, false, 65});
        auto image = std::make_shared<wb::HeightmapData>();
        image->width = image->depth = 2;
        image->values = {0, 65535, 32768, 16384};
        wb::TerrainInstance instance;
        instance.id = "region-smart";
        instance.image = image;
        instance.x = -25;
        instance.z = -20;
        instance.width = 60;
        instance.depth = 40;
        instance.low = 5;
        instance.high = 90;
        instance.blend = 4;
        world.heights.instances.push_back(instance);
        for (int n = 0; n < 3; ++n) {
            wb::SculptLayer layer;
            layer.id = QString("layer-%1").arg(n);
            layer.enabled = n != 2;
            world.heights.layers.push_back(layer);
        }
        for (size_t n = 0; n < world.heights.layers.size(); ++n)
            for (int i = int(n); i < int(world.heights.size()); i += 11)
                world.heights.restoreDelta(world.heights.layers[n].id, i, (i % 13 - 6) * 30);
        world.heights.activeLayer = "layer-0";
        world.setBlock(0, 100, 0, wb::Glass);
        const QRect area(-33, -23, 51, 37);
        for (int offset : {-381, -12, 1, 27, 381}) {
            const auto source = world;
            auto reference = source;
            int changed = 0, clipped = 0;
            for (int z = area.top(); z <= area.bottom(); ++z)
                for (int x = area.left(); x <= area.right(); ++x) {
                    const int i = reference.column(x, z), old = reference.heights.delta(i);
                    const int raw = reference.height(x, z) + offset;
                    clipped += raw < -63 || raw > 318;
                    reference.heights.set(i, std::clamp(raw, -63, 318));
                    changed += old != reference.heights.delta(i);
                }
            int previous = -1, callbacks = 0;
            auto result = wb::prepareHeightRegion(source.heights, area, offset, [&](int done, int total) {
                check(done >= previous && done <= total && total > 0, "monotonic bounded progress");
                previous = done;
                ++callbacks;
                return true;
            });
            check(result.error.isEmpty() && !result.cancelled && result.changed == changed &&
                      result.clipped == clipped && callbacks > 1,
                  "tiled edit matches sequential reference counts");
            check(source.heights == world.heights, "preparation never mutates source");
            auto applied = source;
            applied.apply(result.command, true);
            check(applied.heights == reference.heights && applied.overrides == source.overrides,
                  "staged result matches sequential set across layers, instances and clipping");
            check(result.command.heights.empty() && result.command.cells.empty() &&
                      result.command.heightArea == area,
                  "region command publishes layer snapshot without per-column history");
            applied.apply(result.command, false);
            check(applied.heights == source.heights, "one undo restores full previous layer");
            applied.apply(result.command, true);
            const auto snapshot = applied.heights;
            const int i = applied.column(0, 0);
            applied.heights.set(i, applied.height(0, 0) == 100 ? 101 : 100);
            check(snapshot.layers[1].deltas == result.command.deltaAfter &&
                      applied.heights.layers[1].deltas != result.command.deltaAfter &&
                      *result.command.deltaAfter == *reference.heights.layers[1].deltas,
                  "subsequent stroke detaches and preserves redo snapshot");
            applied.apply(result.command, false);
            applied.apply(result.command, true);
            check(applied.heights == reference.heights, "redo after COW restores exact transaction");
        }
        const auto cancelled =
            wb::prepareHeightRegion(world.heights, area, 5, [](int, int) { return false; });
        check(cancelled.cancelled && !cancelled.command.deltaAfter, "pre-cancel has no published command");
        const auto midTile =
            wb::prepareHeightRegion(world.heights, area, 5, [&](int done, int) { return done < 256; });
        check(midTile.cancelled && midTile.changed > 0 && !midTile.command.deltaAfter,
              "tile cancellation discards partial result");
        QString error;
        for (const QRect invalid : {QRect(), QRect(-100, -100, 2, 2), QRect(0, 0, 100, 100)})
            check(!wb::validateHeightRegion(world.heights, invalid, 1, error), "invalid bounds rejected");
        for (int offset : {0, -382, 382})
            check(!wb::validateHeightRegion(world.heights, area, offset, error), "invalid offset rejected");
        world.heights.activeLayer = "layer-2";
        check(!wb::validateHeightRegion(world.heights, area, 1, error), "hidden active layer rejected");
        world.heights.activeLayer = "missing";
        check(!wb::validateHeightRegion(world.heights, area, 1, error), "missing active layer rejected");
        world.heights.activeLayer = "terrain-base-1";

        auto boundary = wb::World::create(wb::WorldOptions{65, 42, -60, 24, 0, true, 49});
        for (const QRect r : {QRect(boundary.originX, boundary.originZ, 1, 1),
                              QRect(boundary.originX + 15, boundary.originZ + 15, 2, 2),
                              QRect(boundary.originX + 64, boundary.originZ + 48, 1, 1)}) {
            auto result = wb::prepareHeightRegion(boundary.heights, r, 1);
            wb::Command individual;
            for (int z = r.top(); z <= r.bottom(); ++z)
                for (int x = r.left(); x <= r.right(); ++x)
                    individual.heights.push_back({boundary.column(x, z), 0, 1});
            check(wb::affectedChunks(boundary, result.command) == wb::affectedChunks(boundary, individual),
                  "region halo matches per-column dirty chunks at world and chunk edges");
        }
        boundary.heights.fill(318);
        auto noop = wb::prepareHeightRegion(boundary.heights, QRect(0, 0, 5, 7), 10);
        check(noop.changed == 0 && noop.clipped == 35 && !noop.command.deltaAfter,
              "fully clipped no-op creates no command");

        auto large = wb::World::create(wb::WorldOptions{16384, 42, -60, 24, 0, true, 12289});
        const QRect million(5000, 3000, 1024, 1024);
        check(wb::validateHeightRegion(large.heights, million, 6, error), "one million columns allowed");
        check(!wb::validateHeightRegion(large.heights, QRect(5000, 3000, 1025, 1024), 6, error),
              "over limit rejected without truncation");
        QElapsedTimer timer;
        timer.start();
        auto result = wb::prepareHeightRegion(large.heights, million, 6);
        const qint64 prepareMs = timer.elapsed();
        check(result.error.isEmpty() && result.changed == 1048576, "million-column preparation completes");
        check(large.heights.changes().empty(), "million-column prepare leaves source untouched");
        timer.restart();
        large.apply(result.command, true);
        const qint64 commitNs = timer.nsecsElapsed();
        check(large.height(5000, 3000) == 30 && large.height(6023, 4023) == 30 &&
                  large.height(6024, 4023) == 24 && large.heights.changes().size() == 1048576,
              "million-column atomic publication respects inclusive bounds");
        const auto midCopy = wb::prepareHeightRegion(large.heights, QRect(0, 0, 256, 256), 1,
                                                     [](int done, int) { return done < 8192; });
        check(midCopy.cancelled && midCopy.changed > 0 && !midCopy.command.deltaAfter &&
                  large.height(0, 0) == 24,
              "shared existing-layer transaction remains cancellable during evaluation");
        timer.restart();
        check(wb::saveProject(temp.filePath("million.wbench"), large, error), "million-column v8 save");
        const qint64 saveMs = timer.elapsed();
        wb::World loaded;
        check(wb::loadProject(temp.filePath("million.wbench"), loaded, error) &&
                  loaded.heights == large.heights && loaded.width == 16384 && loaded.depth == 12289,
              "million-column v9 exact round trip");
        large.apply(result.command, false);
        check(large.heights.changes().empty() && large.height(5000, 3000) == 24,
              "million-column undo restores original shared map");
        large.apply(result.command, true);
        check(large.heights.layers[0].deltas == result.command.deltaAfter,
              "million-column redo uses already computed snapshot");
        check(large.heights.layers[0].deltas->allocatedBytes() < 6 * 1024 * 1024,
              "million dense deltas use compact pages instead of a node per column");
        const auto retained = large.heights.layers[0].deltas;
        auto local = std::make_shared<wb::HeightDeltas>(*retained);
        std::set<const void *> memory;
        retained->uniqueBytes(memory);
        local->set(large.column(5000, 3000), 8);
        check(local->uniqueBytes(memory) < 128 * 1024 && retained->get(large.column(5000, 3000)) == 6,
              "editing one column copies only a root, one group and one value page");
        const auto overhead = wb::heightRegionWorkspaceBytes(0, million);
        check(overhead < wb::HeightRegionBudget &&
                  wb::heightRegionWorkspaceBytes(wb::HeightRegionBudget - overhead, million) ==
                      wb::HeightRegionBudget &&
                  wb::heightRegionWorkspaceBytes(wb::HeightRegionBudget - overhead + 1, million) >
                      wb::HeightRegionBudget,
              "workspace estimate handles exact budget boundary");
        std::cout << "1M / 16Kx12289: prepare " << prepareMs << " ms, publish " << commitNs << " ns, save "
                  << saveMs << " ms\n";
        std::cout << "Region edit checks: " << checks << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Region edit failed at " << checks << ": " << e.what() << "\n";
        return 1;
    }
}
