#include "RegionEdit.h"
#include <new>

namespace wb {
uint64_t heightRegionWorkspaceBytes(uint64_t residentBytes, QRect area) {
    // Worst-case alignment and group copies; unchanged value pages stay shared.
    const uint64_t pages = (uint64_t(area.width() + 255) / 256 + 1) * area.height();
    return residentBytes +
           pages * (sizeof(HeightDeltas::Page) + 96 + sizeof(HeightDeltas::Group) + 64 * 64 + 96);
}
bool validateHeightRegion(const HeightField &field, QRect area, int offset, QString &error) {
    if (!area.isValid() || !QRect(field.originX, field.originZ, field.width, field.depth).contains(area) ||
        int64_t(area.width()) * area.height() > MaxHeightRegionColumns || offset == 0 || offset < -381 ||
        offset > 381) {
        error = "请选取世界范围内不超过1048576列的区域，输入非零高度偏移（−381…381）";
        return false;
    }
    const int layer = field.layerIndex(field.activeLayer);
    if (layer < 0 || !field.layers[layer].enabled) {
        error = "请先显示当前雕刻层";
        return false;
    }
    if (heightRegionWorkspaceBytes(field.layers[layer].deltas->allocatedBytes(), area) > HeightRegionBudget) {
        error = "本次区域操作的图层快照估算超过256 MiB预算，请缩小范围或使用新的雕刻层";
        return false;
    }
    return true;
}
HeightRegionResult prepareHeightRegion(const HeightField &field, QRect area, int offset,
                                       const std::function<bool(int, int)> &progress) {
    HeightRegionResult result;
    if (!validateHeightRegion(field, area, offset, result.error))
        return result;
    try {
        const auto before = field.layers[field.layerIndex(field.activeLayer)].deltas;
        const int total = area.width() * area.height();
        int done = 0;
        auto keepGoing = [&] { return !progress || progress(done, total); };
        if (!keepGoing()) {
            result.cancelled = true;
            return result;
        }
        auto after = std::make_shared<HeightDeltas>(*before);
        for (int tz = area.top(); tz <= area.bottom(); tz += World::ChunkSize)
            for (int tx = area.left(); tx <= area.right(); tx += World::ChunkSize) {
                for (int z = tz; z <= std::min(tz + World::ChunkSize - 1, area.bottom()); ++z)
                    for (int x = tx; x <= std::min(tx + World::ChunkSize - 1, area.right()); ++x) {
                        const int index = (z - field.originZ) * field.width + x - field.originX;
                        int other = field.base(index);
                        for (const auto &layer : field.layers)
                            if (layer.enabled && layer.id != field.activeLayer) {
                                other += layer.deltas->get(index);
                            }
                        const int oldDelta = before->get(index);
                        const int height = std::clamp(other + oldDelta, -63, 318);
                        const int raw = height + offset, target = std::clamp(raw, -63, 318);
                        result.clipped += raw != target;
                        if (height != target) {
                            const int delta = std::clamp(target - other, -1000000, 1000000);
                            if (delta != oldDelta) {
                                after->set(index, delta);
                                ++result.changed;
                            }
                        }
                        ++done;
                    }
                if (!keepGoing()) {
                    result.cancelled = true;
                    return result;
                }
            }
        if (result.changed) {
            result.command.label = "分块选区高度偏移";
            result.command.heightLayer = field.activeLayer;
            result.command.heightArea = area;
            result.command.deltaBefore = before;
            result.command.deltaAfter = std::move(after);
        }
    } catch (const std::bad_alloc &) {
        result.command = {};
        result.error = "区域编辑内存不足，未修改工程";
    }
    return result;
}
} // namespace wb
