#pragma once
#include "World.h"
#include <functional>

namespace wb {
inline constexpr int MaxHeightRegionColumns = 1024 * 1024;
inline constexpr uint64_t HeightRegionBudget = 256ull * 1024 * 1024;
struct HeightRegionResult {
    Command command;
    QString error;
    int changed = 0, clipped = 0;
    bool cancelled = false;
};
bool validateHeightRegion(const HeightField &field, QRect area, int offset, QString &error);
// Runs on a snapshot. Returning false from progress cancels without publishing
// any world data. Work is processed in 16x16 tiles; callback is never a UI call.
HeightRegionResult prepareHeightRegion(const HeightField &field, QRect area, int offset,
                                       const std::function<bool(int, int)> &progress = {});
} // namespace wb
