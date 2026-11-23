#pragma once
#include "Project.h"
#include <QJsonObject>
namespace wb {
// qCompress framing, but with a fixed output buffer: never grow to fit corrupt input.
bool unpackProjectBlock(const QByteArray &packed, qsizetype limit, QByteArray &raw);
// Shared semantic validation for the legacy JSON envelope and indexed v9 payloads.
struct ProjectPayload {
    std::vector<std::shared_ptr<const HeightmapData>> images;
    std::shared_ptr<const std::vector<int16_t>> dense;
    std::vector<std::shared_ptr<HeightDeltas>> deltas;
    std::unordered_map<uint64_t, Block> overrides;
    std::unordered_set<uint64_t> terrainOverrideKeys;
};
QJsonObject projectMetadata(const World &world, bool inlinePayload);
bool decodeProjectMetadata(const QJsonObject &object, World &world, QString &error,
                           ProjectPayload *payload = nullptr);
bool loadLegacyProject(const QString &path, World &world, QString &error);
} // namespace wb
