#pragma once
#include "World.h"
#include <atomic>
#include <functional>
namespace wb {
struct ExportResult {
    bool ok = false;
    QString path, error;
    int chunks = 0;
};
ExportResult exportMinecraft(const World &world, const QString &directory,
                             std::atomic_bool *cancelled = nullptr,
                             std::function<void(int, int)> progress = {});
} // namespace wb
