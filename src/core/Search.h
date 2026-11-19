#pragma once
#include <QStringList>
namespace wb {
QStringList searchKeys(const QString &name, const QString &title);
// Lower scores rank first; -1 is no match. All query tokens must match.
int searchScore(const QStringList &keys, const QString &query);
} // namespace wb
