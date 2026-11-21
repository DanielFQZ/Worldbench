#include "RegionEdit.h"
#include <new>

namespace wb {
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
    // Conservative map-node estimate for source, staged copy and added columns.
    // This is an edit workspace guard, not a claim about total process memory.
    const uint64_t nodes =
        2 * uint64_t(field.layers[layer].deltas->size()) + uint64_t(area.width()) * area.height();
    if (nodes * 64 > HeightRegionBudget) {
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
        const int total = int(before->size()) + area.width() * area.height();
        int done = 0;
        auto keepGoing = [&] { return !progress || progress(done, total); };
        if (!keepGoing()) {
            result.cancelled = true;
            return result;
        }
        auto after = std::make_shared<std::map<int, int>>();
        // Copy in bounded batches too: cancelling a large existing layer must
        // not wait for an uninterruptible std::map copy constructor.
        for (const auto &entry : *before) {
            after->emplace_hint(after->end(), entry);
            if (++done % 4096 == 0 && !keepGoing()) {
                result.cancelled = true;
                return result;
            }
        }
        for (int tz = area.top(); tz <= area.bottom(); tz += World::ChunkSize)
            for (int tx = area.left(); tx <= area.right(); tx += World::ChunkSize) {
                for (int z = tz; z <= std::min(tz + World::ChunkSize - 1, area.bottom()); ++z)
                    for (int x = tx; x <= std::min(tx + World::ChunkSize - 1, area.right()); ++x) {
                        const int index = (z - field.originZ) * field.width + x - field.originX;
                        int other = field.base(index);
                        for (const auto &layer : field.layers)
                            if (layer.enabled && layer.id != field.activeLayer) {
                                const auto it = layer.deltas->find(index);
                                if (it != layer.deltas->end())
                                    other += it->second;
                            }
                        const auto old = before->find(index);
                        const int oldDelta = old == before->end() ? 0 : old->second;
                        const int height = std::clamp(other + oldDelta, -63, 318);
                        const int raw = height + offset, target = std::clamp(raw, -63, 318);
                        result.clipped += raw != target;
                        if (height != target) {
                            const int delta = std::clamp(target - other, -1000000, 1000000);
                            if (delta != oldDelta) {
                                if (delta == 0)
                                    after->erase(index);
                                else
                                    (*after)[index] = delta;
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
