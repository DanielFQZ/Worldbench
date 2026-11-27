#pragma once
#include "World.h"
#include <functional>
namespace wb {
bool saveProject(const QString &path, const World &world, QString &error,
                 const std::function<bool(QString &)> &beforeCommit = {});
bool loadProject(const QString &path, World &world, QString &error);
} // namespace wb
