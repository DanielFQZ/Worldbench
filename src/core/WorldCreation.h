#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMatrix4x4>
#include <QRect>
#include <QString>
#include <QVector3D>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wb {

// The creation graph is intentionally a small, typed, deterministic subset of
// UE/Houdini PCG. It is a data model first: the same graph is evaluated by the
// preview, export and MCP paths, and unknown node types remain diagnosable.
struct PcgNode {
    QString id;
    QString type;
    QJsonObject parameters;
    bool operator==(const PcgNode &) const = default;
};
struct PcgConnection {
    QString fromNode, fromPort, toNode, toPort;
    bool operator==(const PcgConnection &) const = default;
};
struct PcgGraph {
    QString id, name = "未命名 PCG 图";
    int version = 1;
    bool enabled = true;
    std::vector<PcgNode> nodes;
    std::vector<PcgConnection> connections;
    QStringList diagnostics;
    bool surfaceMaterial = false;
    bool validate(QString &error) const;
    double evaluate(double x, double z, double height, double slope, uint32_t seed,
                    QString *error = nullptr) const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, PcgGraph &, QString &error);
    bool operator==(const PcgGraph &) const = default;
};

struct PcgVolume {
    QString id, name = "PCG 范围";
    QString graphId;
    QVector3D min{-32, 0, -32}, max{32, 128, 32};
    float transition = 8.f;
    uint32_t seed = 1;
    bool enabled = true, visible = true;
    QString sourceId;
    bool contributionOnly = true;
    QJsonObject overrides;
    bool operator==(const PcgVolume &) const = default;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, PcgVolume &, QString &error);
};

struct StructureBlock {
    int x = 0, y = 0, z = 0;
    QString state = "minecraft:air";
    QByteArray nbt;
    bool operator==(const StructureBlock &) const = default;
};
// Imported voxel data is shared by immutable world/history snapshots. Editing
// metadata never duplicates millions of block strings.
template <class T> class SharedValues {
    std::shared_ptr<std::vector<T>> data_ = std::make_shared<std::vector<T>>();
    void detach() {
        if (data_.use_count() != 1)
            data_ = std::make_shared<std::vector<T>>(*data_);
    }

  public:
    size_t size() const { return data_->size(); }
    bool empty() const { return data_->empty(); }
    auto begin() const { return data_->cbegin(); }
    auto end() const { return data_->cend(); }
    const T &operator[](size_t i) const { return (*data_)[i]; }
    void clear() { data_ = std::make_shared<std::vector<T>>(); }
    void reserve(size_t n) {
        detach();
        data_->reserve(n);
    }
    void push_back(T value) {
        detach();
        data_->push_back(std::move(value));
    }
    const void *identity() const { return data_.get(); }
    bool operator==(const SharedValues &o) const { return data_ == o.data_ || *data_ == *o.data_; }
};
struct StructureEntity {
    QVector3D position;
    QByteArray nbt;
    bool operator==(const StructureEntity &) const = default;
};
struct StructureAsset {
    QString id, name, sourcePath, format;
    int width = 0, height = 0, depth = 0;
    QVector3D pivot{0, 0, 0};
    int groundBlend = 2;
    QString placement = "place"; // place, carve, ignore
    QStringList markers;
    SharedValues<StructureBlock> blocks;
    SharedValues<StructureEntity> entities;
    QString category; // plant, building or cave. Empty legacy values follow placement.
    QString diagnostic;
    QVector3D defaultRotation, defaultScale{1, 1, 1};
    bool operator==(const StructureAsset &) const = default;
    QJsonObject toJson(bool includeBlocks = true) const;
    static bool fromJson(const QJsonObject &, StructureAsset &, QString &error);
};

struct AssetPreset {
    QString id, name, assetId, filePath;
    QVector3D position, rotation, scale{1, 1, 1};
    bool enabled = true;
    bool operator==(const AssetPreset &) const = default;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, AssetPreset &, QString &error);
};

QMatrix4x4 placementMatrix(const AssetPreset &placement);

struct RoadNode {
    QString id, name;
    QVector3D position;
    bool enabled = true;
    bool operator==(const RoadNode &) const = default;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, RoadNode &, QString &error);
};
struct RoadEdge {
    QString id, from, to;
    int width = 5;
    QString surface = "minecraft:gravel";
    bool bridge = false, tunnel = false, enabled = true;
    float maxSlope = 30;
    int clearance = 5;
    bool operator==(const RoadEdge &) const = default;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, RoadEdge &, QString &error);
};

struct CaveMarker {
    QString id, assetId, kind = "portal"; // portal, interior, seal, exit
    QVector3D position;
    bool enabled = true;
    QVector3D end;
    bool operator==(const CaveMarker &) const = default;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, CaveMarker &, QString &error);
};

struct PcgAsset {
    QString id, name;
    PcgVolume volume;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, PcgAsset &, QString &error);
    bool operator==(const PcgAsset &) const = default;
};
struct CreationState {
    QStringList folders;
    QMap<QString, QString> assetFolders;
    std::vector<PcgAsset> pcgAssets;
    std::vector<PcgGraph> graphs;
    std::vector<PcgVolume> volumes;
    std::vector<StructureAsset> assets;
    std::vector<AssetPreset> presets;
    std::vector<RoadNode> roadNodes;
    std::vector<RoadEdge> roadEdges;
    std::vector<CaveMarker> caveMarkers;
    bool operator==(const CreationState &) const = default;
};

const PcgGraph *libraryGraph(const CreationState &state, const QString &id);
QStringList scatterReferences(const PcgGraph &graph);
// Imports a legacy embedded graph or an expanded exchange manifest into a staged project.
bool indexGraph(CreationState &state, const QJsonObject &bundle, QString &graphId, QString &error,
                bool authoritativeIds = false);

struct PcgSample {
    double x = 0, z = 0, height = 0, baseHeight = 0, slope = 0, localX = 0, localZ = 0, mask = 1;
    uint32_t seed = 1;
    double input = 1;
    const QJsonObject *parameters = nullptr;
};
struct PcgPlan;
struct PcgInstruction {
    QString type;
    QString id;
    QJsonObject parameters;
    int a = -1, b = -1, t = -1, value = -1;
    std::shared_ptr<const PcgPlan> subgraph;
};
struct PcgPlan {
    std::vector<PcgInstruction> instructions;
    int output = -1;
    double evaluate(const PcgSample &sample) const;
    std::vector<double> evaluateValues(const PcgSample &sample) const;
};
bool compileGraph(const PcgGraph &graph, PcgPlan &plan, QString &error);
QJsonArray pcgNodeCatalog();
bool validateCreation(const CreationState &state, QString &error);
class World;
struct GeneratedCreation {
    struct Object {
        QString id, ownerId;
        AssetPreset placement;
    };
    std::vector<Object> objects;
    // Ownership is derived only. Export still uses the complete cells map.
    std::unordered_map<uint64_t, size_t> objectCells;
    std::unordered_map<uint64_t, uint32_t> cells;
    std::unordered_map<int, int> heights;
    std::vector<QVector3D> scatterPoints;
    QStringList diagnostics;
    std::set<int> geometryChunks;
    std::unordered_set<uint64_t> geometryCells;
    std::unordered_map<int, std::vector<std::pair<uint64_t, uint32_t>>> byChunk;
    std::vector<QRect> surfaceHoles;
    std::unordered_map<uint64_t, QByteArray> blockEntities;
    std::vector<StructureEntity> entities;
};
using CreationProgress = std::function<bool(int, const QString &)>;
std::shared_ptr<const GeneratedCreation> generateCreation(World &world, QString &error,
                                                          const CreationProgress &progress = {});
bool importStructure(const QString &path, StructureAsset &asset, QString &error);
bool parseCreationBlock(const QString &text, struct BlockState &state, QString &error);
struct CaveAnalysis;
CaveAnalysis analyzeAssetCave(const StructureAsset &asset, const std::vector<CaveMarker> &markers);

QJsonObject saveCreation(const CreationState &state);
bool loadCreation(const QJsonObject &object, CreationState &state, QString &error);
bool importStructureJson(const QString &path, StructureAsset &asset, QString &error);

struct CaveAnalysis {
    int width = 0, height = 0, depth = 0;
    std::vector<uint8_t> labels; // 0 outside, 1 inside, 2 portal, 3 leak, 4 unconfirmed chamber
    int chambers = 0;
    int unconfirmedChambers = 0;
    QStringList diagnostics;
    bool valid = false;
};
CaveAnalysis analyzeCave(const std::vector<uint8_t> &solid, int width, int height, int depth,
                         const std::vector<QVector3D> &portals, const std::vector<QVector3D> &interiorSeeds);
std::vector<QVector3D> buildRoadPolyline(const std::vector<RoadNode> &nodes, const RoadEdge &edge,
                                         float spacing = 2.f);

} // namespace wb
