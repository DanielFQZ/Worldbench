#include "World.h"
#include <algorithm>
#include <cmath>
namespace wb {
void HeightField::configure(const WorldOptions &o, int x, int z) {
    width = o.width;
    depth = o.depth ? o.depth : o.width;
    originX = x;
    originZ = z;
    seed = o.seed;
    baseHeight = o.baseHeight;
    variation = o.variation;
    flat = o.flat;
    dense.reset();
    instances.clear();
    layers = {SculptLayer{}};
    activeLayer = "terrain-base-1";
}
int16_t HeightField::base(int i) const {
    const double x = i % width + originX, z = i / width + originZ, phase = (seed % 997) * .009;
    double value = baseHeight;
    if (dense)
        value = (*dense)[i];
    else if (!flat && variation) {
        const double noise = .55 * std::sin(x * .045 + phase) * std::cos(z * .05 - phase) +
                             .3 * std::cos(x * .09 + z * .04 + phase) + .15 * std::sin(z * .13 - phase);
        value = std::clamp(int(std::lround(baseHeight + variation * noise)), -63, 318);
    }
    for (const auto &instance : instances)
        value = instance.apply(value, int(x), int(z));
    return int16_t(std::clamp(int(std::lround(value)), -63, 318));
}
int HeightField::layerIndex(const QString &id) const {
    for (size_t n = 0; n < layers.size(); ++n)
        if (layers[n].id == id)
            return int(n);
    return -1;
}
int HeightField::delta(int i) const {
    const auto &edits = changes();
    const auto it = edits.find(i);
    return it == edits.end() ? 0 : it->second;
}
int16_t HeightField::operator[](int i) const {
    int value = base(i);
    for (const auto &layer : layers)
        if (layer.enabled) {
            const auto it = layer.deltas->find(i);
            if (it != layer.deltas->end())
                value += it->second;
        }
    return int16_t(std::clamp(value, -63, 318));
}
void HeightField::restoreDelta(const QString &id, int i, int value) {
    const int n = layerIndex(id);
    if (n < 0)
        return;
    auto &edits = layers[n].deltas;
    if (edits.use_count() != 1)
        edits = std::make_shared<std::map<int, int>>(*edits);
    if (value == 0)
        edits->erase(i);
    else
        (*edits)[i] = value;
}
void HeightField::set(int i, int v) {
    const int n = layerIndex(activeLayer);
    if (n < 0 || !layers[n].enabled || (*this)[i] == v)
        return;
    int other = base(i);
    for (const auto &l : layers)
        if (l.enabled && l.id != activeLayer) {
            const auto it = l.deltas->find(i);
            if (it != l.deltas->end())
                other += it->second;
        }
    restoreDelta(activeLayer, i, std::clamp(v - other, -1000000, 1000000));
}
void HeightField::fill(int v) {
    dense.reset();
    instances.clear();
    flat = true;
    baseHeight = v;
    variation = 0;
    layers = {SculptLayer{}};
    activeLayer = "terrain-base-1";
}
void HeightField::restore(int i, int value, bool overridden) {
    // Legacy v3/v4 absolute heights migrate to deltas preserving their current appearance.
    restoreDelta(activeLayer, i, overridden ? value - base(i) : 0);
}
void HeightField::assign(std::vector<int16_t> v) {
    dense = std::make_shared<const std::vector<int16_t>>(std::move(v));
    layers = {SculptLayer{}};
    activeLayer = "terrain-base-1";
}
bool HeightField::operator==(const HeightField &o) const {
    if (width != o.width || depth != o.depth)
        return false;
    const bool sameBase = dense || o.dense
                              ? dense == o.dense || (dense && o.dense && *dense == *o.dense)
                              : originX == o.originX && originZ == o.originZ && seed == o.seed &&
                                    baseHeight == o.baseHeight && variation == o.variation && flat == o.flat;
    if (sameBase && instances == o.instances)
        return layers == o.layers;
    for (int i = 0; i < int(size()); ++i)
        if ((*this)[i] != o[i])
            return false;
    return true;
}
} // namespace wb
