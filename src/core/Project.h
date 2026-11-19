#pragma once
#include "World.h"
namespace wb {
bool saveProject(const QString &path, const World &world, QString &error);
bool loadProject(const QString &path, World &world, QString &error);
} // namespace wb
