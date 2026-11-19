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
    m_edits = std::make_shared<std::map<int, int16_t>>();
}
int16_t HeightField::base(int i) const {
    if (dense)
        return (*dense)[i];
    if (flat || !variation)
        return int16_t(baseHeight);
    const double x = i % width + originX, z = i / width + originZ, phase = (seed % 997) * .009;
    const double noise = .55 * std::sin(x * .045 + phase) * std::cos(z * .05 - phase) +
                         .3 * std::cos(x * .09 + z * .04 + phase) + .15 * std::sin(z * .13 - phase);
    return int16_t(std::clamp(int(std::lround(baseHeight + variation * noise)), -63, 318));
}
int16_t HeightField::operator[](int i) const {
    const auto it = m_edits->find(i);
    return it == m_edits->end() ? base(i) : it->second;
}
void HeightField::set(int i, int v) {
    if ((*this)[i] == v)
        return;
    if (m_edits.use_count() != 1)
        m_edits = std::make_shared<std::map<int, int16_t>>(*m_edits);
    if (base(i) == v)
        m_edits->erase(i);
    else
        (*m_edits)[i] = int16_t(v);
}
void HeightField::fill(int v) {
    dense.reset();
    flat = true;
    baseHeight = v;
    variation = 0;
    m_edits = std::make_shared<std::map<int, int16_t>>();
}
void HeightField::assign(std::vector<int16_t> v) {
    dense = std::make_shared<const std::vector<int16_t>>(std::move(v));
    m_edits = std::make_shared<std::map<int, int16_t>>();
}
bool HeightField::operator==(const HeightField &o) const {
    if (width != o.width || depth != o.depth)
        return false;
    const bool sameBase = dense || o.dense
                              ? dense == o.dense || (dense && o.dense && *dense == *o.dense)
                              : originX == o.originX && originZ == o.originZ && seed == o.seed &&
                                    baseHeight == o.baseHeight && variation == o.variation && flat == o.flat;
    if (sameBase)
        return *m_edits == *o.m_edits;
    for (int i = 0; i < int(size()); ++i)
        if ((*this)[i] != o[i])
            return false;
    return true;
}
} // namespace wb
