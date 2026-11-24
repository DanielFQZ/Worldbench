#include "WorldCreation.h"
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace wb {
const PcgGraph *libraryGraph(const CreationState &state, const QString &id) {
    QString key = id;
    for (const auto &preset : state.pcgAssets)
        if (preset.id == id)
            key = preset.volume.graphId;
    for (const auto &graph : state.graphs)
        if (graph.id == key)
            return &graph;
    return nullptr;
}
namespace {
void visitScatter(PcgGraph &graph, const std::function<QString(QString)> &resolve) {
    for (auto &node : graph.nodes) {
        if (node.type != "output_scatter")
            continue;
        auto &p = node.parameters;
        if (!p["assetId"].toString().isEmpty())
            p["assetId"] = resolve(p["assetId"].toString());
        if (!p.contains("assets"))
            continue;
        auto entries = p["assets"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            entry["id"] = resolve(entry["id"].toString());
            entries[i] = entry;
        }
        p["assets"] = entries;
    }
}
StructureAsset comparable(StructureAsset a) {
    a.id.clear();
    a.name.clear();
    a.sourcePath.clear();
    a.format.clear();
    a.diagnostic.clear();
    return a;
}
std::vector<CaveMarker> markersFor(const std::vector<CaveMarker> &all, const QString &id) {
    std::vector<CaveMarker> result;
    for (auto marker : all)
        if (marker.assetId == id) {
            marker.assetId.clear();
            marker.id.clear();
            result.push_back(marker);
        }
    return result;
}
} // namespace
QStringList scatterReferences(const PcgGraph &graph) {
    QStringList ids;
    auto copy = graph;
    visitScatter(copy, [&](QString id) {
        if (!ids.contains(id))
            ids.append(id);
        return id;
    });
    return ids;
}
bool indexGraph(CreationState &state, const QJsonObject &bundle, QString &graphId, QString &error,
                bool authoritativeIds) {
    PcgGraph graph;
    if (!PcgGraph::fromJson(bundle.contains("graph") ? bundle["graph"].toObject() : bundle, graph, error))
        return false;
    QMap<QString, QString> refs;
    std::vector<CaveMarker> markers;
    for (const auto value : bundle["caveMarkers"].toArray()) {
        CaveMarker marker;
        if (!CaveMarker::fromJson(value.toObject(), marker, error))
            return false;
        markers.push_back(marker);
    }
    for (const auto value : bundle["assets"].toArray()) {
        StructureAsset asset;
        if (!StructureAsset::fromJson(value.toObject(), asset, error))
            return false;
        const auto source = asset.id;
        const auto signature = comparable(asset);
        const auto cave = markersFor(markers, source);
        bool collision = false;
        QString found;
        if (refs.contains(source)) {
            error = "结构依赖ID重复";
            return false;
        }
        for (const auto &existing : state.assets) {
            const bool sameId = existing.id == source;
            collision |= sameId;
            if ((sameId && authoritativeIds) ||
                (comparable(existing) == signature && markersFor(state.caveMarkers, existing.id) == cave)) {
                found = existing.id;
                if (sameId)
                    break;
            }
        }
        if (found.isEmpty()) {
            if (collision)
                asset.id = QUuid::createUuid().toString(QUuid::Id128);
            found = asset.id;
            state.assets.push_back(std::move(asset));
            for (auto marker : cave) {
                marker.id = QUuid::createUuid().toString(QUuid::Id128);
                marker.assetId = found;
                state.caveMarkers.push_back(marker);
            }
        }
        refs[source] = found;
    }
    for (const auto &marker : markers)
        if (!refs.contains(marker.assetId)) {
            error = "洞穴标记引用了缺失的依赖";
            return false;
        }
    visitScatter(graph, [&](QString id) { return refs.value(id, id); });
    if (const auto *sameId = libraryGraph(state, graph.id)) {
        if (authoritativeIds) {
            graphId = sameId->id;
            return true;
        }
        // Old portable copies could be independently edited while retaining the source ID.
        // Reuse a previously migrated variant only when its behaviour matches.
        for (const auto &existing : state.graphs) {
            auto candidate = existing;
            candidate.id = graph.id;
            candidate.name = graph.name;
            if (candidate == graph) {
                graphId = existing.id;
                return true;
            }
        }
        graph.id = QUuid::createUuid().toString(QUuid::Id128);
    }
    graphId = graph.id;
    state.graphs.push_back(std::move(graph));
    return true;
}
} // namespace wb
