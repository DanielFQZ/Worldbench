#include "Editor.h"
#include "Viewport.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <type_traits>
namespace {
QString fresh(const char *prefix) {
    return QString(prefix) + "-" + QUuid::createUuid().toString(QUuid::Id128);
}
QVariantList rows(const QJsonArray &a) {
    return a.toVariantList();
}
QJsonArray xyz(float x, float y, float z) {
    return {x, y, z};
}
// Exchange files reference immutable sibling records. The project keeps its
// own indexed records so reopening a saved world never needs the source files.
constexpr qint64 ExchangeLimit = 64 * 1024 * 1024;
QString recordPath(const QString &manifest, const QString &hash) {
    return QFileInfo(manifest).dir().filePath("Worldbench.dependencies/" + hash + ".wbdep");
}
bool saveRecord(const QString &manifest, const QJsonObject &record, QString &hash, QString &error) {
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (bytes.size() > ExchangeLimit) {
        error = "单项资产依赖超过64MiB";
        return false;
    }
    hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const auto path = recordPath(manifest, hash);
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() == bytes.size() && existing.readAll() == bytes)
        return true;
    existing.close();
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        error = "无法写入资产依赖 " + path;
        return false;
    }
    return true;
}
bool readRecord(const QString &manifest, const QString &hash, QJsonObject &record, QString &error) {
    static const QRegularExpression valid("^[a-f0-9]{64}$");
    QFile f(recordPath(manifest, hash));
    if (valid.match(hash).hasMatch() && f.size() <= ExchangeLimit && f.open(QIODevice::ReadOnly)) {
        const auto bytes = f.readAll();
        if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == hash.toLatin1()) {
            record = QJsonDocument::fromJson(bytes).object();
            if (!record.isEmpty())
                return true;
        }
    }
    error = "缺少或损坏的资产依赖，请同时复制 Worldbench.dependencies 目录：" + hash;
    return false;
}
bool graphManifest(const wb::CreationState &state, const wb::PcgGraph &graph, const QString &path,
                   QJsonObject &manifest, QString &error) {
    QJsonArray dependencies;
    qint64 total = 0;
    const auto refs = wb::scatterReferences(graph);
    for (const auto &asset : state.assets) {
        if (!refs.contains(asset.id))
            continue;
        QJsonArray markers;
        for (const auto &marker : state.caveMarkers)
            if (marker.assetId == asset.id)
                markers.append(marker.toJson());
        QString hash;
        if (!saveRecord(path, {{"asset", asset.toJson()}, {"caveMarkers", markers}}, hash, error))
            return false;
        total += QFileInfo(recordPath(path, hash)).size();
        if (total > ExchangeLimit) {
            error = "图的结构依赖超过64MiB";
            return false;
        }
        dependencies.append(QJsonObject{{"id", asset.id}, {"record", hash}});
    }
    manifest = {{"version", 2}, {"graph", graph.toJson()}, {"dependencies", dependencies}};
    return true;
}
bool loadIndexedGraph(wb::CreationState &state, const QJsonObject &manifest, const QString &path,
                      QString &graphId, QString &error) {
    if (!manifest.contains("dependencies"))
        return wb::indexGraph(state, manifest, graphId, error);
    wb::PcgGraph graph;
    if (manifest["version"].toInt() != 2 ||
        !wb::PcgGraph::fromJson(manifest["graph"].toObject(), graph, error))
        return false;
    for (const auto &existing : state.graphs)
        if (existing.id == graph.id) {
            graphId = existing.id;
            return true;
        }
    QJsonArray assets, markers;
    qint64 bytes = 0;
    for (const auto value : manifest["dependencies"].toArray()) {
        const auto ref = value.toObject();
        const auto id = ref["id"].toString();
        if (std::any_of(state.assets.begin(), state.assets.end(), [&](const auto &a) { return a.id == id; }))
            continue;
        QJsonObject record;
        const auto hash = ref["record"].toString();
        if (!readRecord(path, hash, record, error))
            return false;
        bytes += QFileInfo(recordPath(path, hash)).size();
        if (bytes > ExchangeLimit || record["asset"].toObject()["id"] != id) {
            error = "依赖标识不匹配或图的结构依赖超过64MiB";
            return false;
        }
        assets.append(record["asset"]);
        for (const auto marker : record["caveMarkers"].toArray())
            markers.append(marker);
    }
    return wb::indexGraph(state, {{"graph", graph.toJson()}, {"assets", assets}, {"caveMarkers", markers}},
                          graphId, error, true);
}
} // namespace
QVariantMap Editor::creationState() const {
    const bool cacheable = !m_creationStart && !m_creationPending;
    if (cacheable && m_creationSummaryRevision == m_revision)
        return m_creationSummary;
    QJsonObject data;
    QJsonArray graphs, volumes, presets, nodes, edges, markers;
    for (auto &v : m_world.creation.graphs)
        graphs.append(v.toJson());
    for (auto &v : m_world.creation.volumes)
        volumes.append(v.toJson());
    for (auto &v : m_world.creation.presets)
        presets.append(v.toJson());
    for (auto &v : m_world.creation.roadNodes)
        nodes.append(v.toJson());
    for (auto &v : m_world.creation.roadEdges)
        edges.append(v.toJson());
    for (auto &v : m_world.creation.caveMarkers)
        markers.append(v.toJson());
    data = {{"graphs", graphs},   {"volumes", volumes}, {"presets", presets},
            {"roadNodes", nodes}, {"roadEdges", edges}, {"caveMarkers", markers}};
    QJsonArray assets;
    for (auto &a : m_world.creation.assets) {
        auto o = a.toJson(false);
        o["blockCount"] = int(a.blocks.size());
        assets.append(o);
    }
    data["assets"] = assets;
    QJsonArray pcgAssets;
    for (const auto &a : m_world.creation.pcgAssets)
        pcgAssets.append(a.toJson());
    data["pcgAssets"] = pcgAssets;
    data["generatedCells"] = m_world.generated ? int(m_world.generated->cells.size()) : 0;
    data["diagnostics"] =
        m_world.generated ? QJsonArray::fromStringList(m_world.generated->diagnostics) : QJsonArray{};
    data["scatterCount"] = m_world.generated ? int(m_world.generated->scatterPoints.size()) : 0;
    auto summary = data.toVariantMap();
    if (cacheable) {
        m_creationSummary = summary;
        m_creationSummaryRevision = m_revision;
    }
    return summary;
}
QVariantMap Editor::creationDetails() const {
    auto data = creationState();
    for (auto key : {"volumes", "presets", "roadNodes", "roadEdges"})
        for (auto value : data[key].toList()) {
            auto row = value.toMap();
            if (row["id"].toString() == m_selectedNode) {
                row["kind"] = key;
                if (row.contains("graphId")) {
                    const auto *graph = wb::libraryGraph(m_world.creation, row["graphId"].toString());
                    row["surfaceMaterial"] = graph && graph->surfaceMaterial;
                }
                row["valid"] = true;
                return row;
            }
        }
    return {{"valid", false}};
}
QVariantList Editor::rangeDependencies(const QString &id) const {
    QVariantList rows;
    if (const auto *graph = wb::libraryGraph(m_world.creation, id)) {
        const auto ids = wb::scatterReferences(*graph);
        for (const auto &asset : m_world.creation.assets)
            if (ids.contains(asset.id))
                rows.append(QVariantMap{{"id", asset.id}, {"name", asset.name}});
    }
    return rows;
}
QVariantList Editor::pcgGraphs() const {
    return creationState()["graphs"].toList();
}
QVariantList Editor::pcgVolumes() const {
    QVariantList rows;
    for (const auto &volume : m_world.creation.volumes) {
        auto row = volume.toJson().toVariantMap();
        const auto r = volume.footprint();
        row["footprint"] = QVariantList{r.x(), r.y(), r.width(), r.height()};
        rows.append(row);
    }
    return rows;
}
QVariantList Editor::structureAssets() const {
    return creationState()["assets"].toList();
}
QVariantList Editor::libraryAssets() const {
    if (m_libraryRevision == m_revision)
        return m_libraryRows;
    QVariantList rows;
    auto add = [&](const QString &id, const QString &name, const QString &kind, const QString &collection,
                   int width, int height, int depth, size_t previewKey) {
        rows.append(QVariantMap{
            {"id", id},
            {"name", name},
            {"kind", kind},
            {"previewKey",
             QString::number(qHashMulti(0, previewKey, quintptr(m_world.assets.get()), m_resourceRevision))},
            {"collection", collection},
            {"folder", m_world.creation.assetFolders.value(id)},
            {"width", width},
            {"height", height},
            {"depth", depth}});
    };
    for (const auto &a : m_world.creation.assets) {
        const auto kind = a.isOpen() ? QStringLiteral("open") : QStringLiteral("closed");
        add(a.id, a.name, kind, "assets", a.width, a.height, a.depth,
            qHashMulti(0, quintptr(a.blocks.identity()), a.width, a.height, a.depth));
    }
    for (const auto &a : m_world.creation.pcgAssets) {
        const auto size = a.volume.max - a.volume.min;
        const auto *graph = wb::libraryGraph(m_world.creation, a.volume.graphId);
        add(a.id, a.name, graph && graph->surfaceMaterial ? "materialRange" : "range", "pcgAssets", size.x(),
            size.y(), size.z(),
            qHashMulti(0, QJsonObject::fromVariantMap(graphData(a.id)), a.volume.toJson()));
    }
    for (const auto &g : m_world.creation.graphs)
        add(g.id, g.name, g.surfaceMaterial ? "material" : "graph", "graphs", 64, 64, 64, qHash(g.toJson()));
    for (const auto &t : m_world.heights.instances)
        add(t.id, t.name, "terrain", "terrains", t.width, t.high - t.low, t.depth,
            qHashMulti(0, quintptr(t.image.get()), t.width, t.depth, t.high - t.low, t.scaleX, t.scaleY,
                       t.scaleZ, t.rotationX, t.rotationY, t.rotationZ));
    m_libraryRevision = m_revision;
    m_libraryRows = rows;
    return rows;
}
namespace {
QString numberedName(QString base, const QSet<QString> &used, bool alwaysNumber) {
    if (base.isEmpty())
        base = "未命名资产";
    if (!alwaysNumber && !used.contains(base.toCaseFolded()))
        return base;
    for (int i = 1;; ++i) {
        const auto suffix = QString::number(i);
        QString candidate = base.left(128 - suffix.size()) + suffix;
        if (!used.contains(candidate.toCaseFolded()))
            return candidate;
    }
}
QString instanceName(const QString &base, const wb::CreationState &state) {
    QSet<QString> used;
    for (const auto &p : state.presets)
        used.insert(p.name.toCaseFolded());
    for (const auto &v : state.volumes)
        used.insert(v.name.toCaseFolded());
    return numberedName(base, used, true);
}
} // namespace
bool Editor::prepareLibraryNames(wb::CreationState &creation, std::vector<wb::TerrainInstance> &terrains,
                                 bool migrate, QString &error) const {
    QHash<QString, QString> existing;
    if (!migrate)
        for (const auto &v : libraryAssets()) {
            const auto row = v.toMap();
            existing.insert(row["id"].toString(), row["name"].toString());
        }
    QSet<QString> used;
    QList<QString *> additions, renames;
    auto collect = [&](auto &items) {
        for (auto &a : items) {
            if (!existing.contains(a.id))
                additions.append(&a.name);
            else if (existing[a.id] != a.name)
                renames.append(&a.name);
            else
                used.insert(a.name.toCaseFolded());
        }
    };
    collect(creation.assets);
    collect(creation.graphs);
    collect(creation.pcgAssets);
    collect(terrains);
    for (auto *name : renames) {
        *name = name->trimmed();
        if (name->isEmpty() || name->size() > 128 || used.contains(name->toCaseFolded())) {
            error = "资产名为空、超过128字或与已有资产重名";
            return false;
        }
        used.insert(name->toCaseFolded());
    }
    for (auto *name : additions) {
        *name = numberedName(name->trimmed().left(128), used, false);
        used.insert(name->toCaseFolded());
    }
    if (migrate)
        for (auto &p : creation.presets) {
            static const QRegularExpression legacy("^资产实例 [0-9]+$");
            if (legacy.match(p.name).hasMatch())
                for (const auto &a : creation.assets)
                    if (a.id == p.assetId) {
                        p.name = instanceName(a.name, creation);
                        break;
                    }
        }
    return true;
}
bool Editor::createAssetFolder(const QString &path) {
    auto next = m_world.creation;
    const auto parts = path.split('/');
    if (path.size() > 256 || next.folders.contains(path, Qt::CaseInsensitive) ||
        std::any_of(parts.begin(), parts.end(),
                    [](const auto &p) {
                        return p.isEmpty() || p != p.trimmed() || p == "." || p == ".." || p.contains('\\');
                    }) ||
        (parts.size() > 1 && !next.folders.contains(path.section('/', 0, -2)))) {
        emit failure("文件夹名称无效、已存在或父目录不存在");
        return false;
    }
    next.folders.append(path);
    next.folders.sort(Qt::CaseInsensitive);
    return commitCreation(std::move(next), "新建资产文件夹");
}
bool Editor::removeAssetFolder(const QString &path) {
    auto next = m_world.creation;
    if (!next.folders.contains(path))
        return false;
    for (const auto &v : libraryAssets())
        if (v.toMap()["folder"] == path) {
            emit failure("请先移出文件夹内的资产");
            return false;
        }
    for (const auto &f : next.folders)
        if (f.startsWith(path + "/")) {
            emit failure("请先移出子文件夹");
            return false;
        }
    next.folders.removeAll(path);
    for (auto it = next.assetFolders.begin(); it != next.assetFolders.end();) {
        if (it.value() == path)
            it = next.assetFolders.erase(it);
        else
            ++it;
    }
    return commitCreation(std::move(next), "删除空文件夹");
}
bool Editor::moveLibraryAsset(const QString &id, const QString &folder) {
    auto next = m_world.creation;
    if (!folder.isEmpty() && !next.folders.contains(folder))
        return false;
    for (const auto &v : libraryAssets())
        if (v.toMap()["id"] == id) {
            if (folder.isEmpty())
                next.assetFolders.remove(id);
            else
                next.assetFolders[id] = folder;
            return commitCreation(std::move(next), "移动库资产");
        }
    return false;
}
bool Editor::renameLibraryAsset(const QString &id, const QString &name) {
    for (const auto &v : libraryAssets()) {
        const auto row = v.toMap();
        if (row["id"] != id)
            continue;
        if (row["collection"] == "terrains") {
            auto next = m_world.heights.instances;
            for (auto &t : next)
                if (t.id == id)
                    t.name = name;
            commitTerrain(std::move(next), "重命名地貌");
            for (const auto &t : m_world.heights.instances)
                if (t.id == id)
                    return t.name == name.trimmed();
        }
        return creationEdit(
            "patch", {{"kind", row["collection"]}, {"id", id}, {"changes", QVariantMap{{"name", name}}}});
    }
    return false;
}
bool Editor::storePcgAsset(const QString &id) {
    wb::PcgAsset asset;
    auto next = m_world.creation;
    auto volume = std::find_if(next.volumes.begin(), next.volumes.end(), [&](auto &v) { return v.id == id; });
    if (volume == next.volumes.end())
        return false;
    auto graph = std::find_if(next.graphs.begin(), next.graphs.end(),
                              [&](auto &g) { return g.id == volume->graphId; });
    if (graph == next.graphs.end())
        return false;
    asset.id = fresh("pcg-asset");
    asset.name = volume->name;
    asset.volume = *volume;
    asset.volume.sourceId.clear();
    asset.volume.enabled = asset.volume.visible = true;
    const int centerX = int((volume->min.x() + volume->max.x()) * .5f),
              centerZ = int((volume->min.z() + volume->max.z()) * .5f);
    const auto anchor =
        QVector3D(float(centerX), float(m_world.height(centerX, centerZ) + 1), float(centerZ));
    asset.volume.min -= anchor;
    asset.volume.max -= anchor;
    next.pcgAssets.push_back(asset);
    return commitCreation(std::move(next), "PCG范围加入资产库");
}
QVariantList Editor::assetPresets() const {
    return creationState()["presets"].toList();
}
QVariantList Editor::roadNodes() const {
    return creationState()["roadNodes"].toList();
}
QVariantList Editor::roadEdges() const {
    return creationState()["roadEdges"].toList();
}
QVariantList Editor::caveMarkers() const {
    return creationState()["caveMarkers"].toList();
}
QVariantMap Editor::graphData(const QString &id) const {
    if (const auto *graph = wb::libraryGraph(m_world.creation, id))
        return graph->toJson().toVariantMap();
    return {};
}
QVariantMap Editor::assetData(const QString &id) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            auto data = a.toJson(false);
            QJsonArray blocks;
            size_t step = std::max(size_t(1), (a.blocks.size() + 8191) / 8192);
            for (size_t i = 0; i < a.blocks.size(); i += step) {
                auto &b = a.blocks[i];
                blocks.append(QJsonArray{b.x, b.y, b.z, b.state});
            }
            data["resourceRevision"] = double(m_resourceRevision);
            data["previewBlocks"] = blocks;
            data["blockCount"] = int(a.blocks.size());
            return data.toVariantMap();
        }
    return {};
}
QVariantMap Editor::caveAnalysis(const QString &id) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            auto r = wb::analyzeAssetCave(a, m_world.creation.caveMarkers);
            int inside = 0, portals = 0;
            for (auto label : r.labels) {
                inside += label == 1;
                portals += label == 2;
            }
            QVariantList issues;
            for (auto &s : r.diagnostics)
                issues << s;
            return {{"valid", r.valid},
                    {"interior", inside},
                    {"portals", portals},
                    {"chambers", r.chambers},
                    {"unconfirmedChambers", r.unconfirmedChambers},
                    {"diagnostics", issues}};
        }
    return {{"valid", false}, {"diagnostics", QStringList{"资产不存在"}}};
}
bool Editor::creationEdit(const QString &action, const QVariantMap &data) {
    if (assetBusy() || m_creationStart) {
        emit failure("请等待当前创作操作完成");
        return false;
    }
    auto next = m_world.creation;
    auto d = QJsonObject::fromVariantMap(data);
    QString error;
    if (action == "replace") {
        if (!wb::loadCreation(d, next, error)) {
            emit failure(error);
            return false;
        }
    } else if (action == "graph") {
        const auto id = d["id"].toString();
        const auto *resolved = wb::libraryGraph(next, id);
        auto *target = resolved ? &next.graphs[size_t(resolved - next.graphs.data())] : nullptr;
        if (!target) {
            emit failure("图不存在");
            return false;
        }
        d["id"] = target->id;
        if (!wb::PcgGraph::fromJson(d, *target, error)) {
            emit failure(error);
            return false;
        }
    } else if (action == "add" || action == "patch" || action == "remove") {
        const QString kind = d["kind"].toString(), id = d["id"].toString();
        const auto changes = d["changes"].toObject();
        auto edit = [&]<class T>(std::vector<T> &items) {
            auto it = std::find_if(items.begin(), items.end(), [&](auto &v) { return v.id == id; });
            if (action != "add" && it == items.end()) {
                error = "对象不存在";
                return false;
            }
            if (action == "remove") {
                items.erase(it);
                return true;
            }
            QJsonObject obj;
            wb::SharedValues<wb::StructureBlock> shared;
            if (action != "add") {
                if constexpr (std::is_same_v<T, wb::StructureAsset>) {
                    obj = it->toJson(false);
                    shared = it->blocks;
                } else
                    obj = it->toJson();
            }
            for (auto i = changes.begin(); i != changes.end(); ++i)
                if (i.key() != "id" || action == "add")
                    obj[i.key()] = i.value();
            if (action == "add" && !obj.contains("id"))
                obj["id"] = fresh("object");
            T value;
            if (!T::fromJson(obj, value, error))
                return false;
            if constexpr (std::is_same_v<T, wb::StructureAsset>)
                if (action != "add" && !changes.contains("blocks"))
                    value.blocks = shared;
            if (action == "add")
                items.push_back(std::move(value));
            else
                *it = std::move(value);
            return true;
        };
        bool ok = false;
        if (kind == "graphs")
            ok = edit(next.graphs);
        else if (kind == "volumes")
            ok = edit(next.volumes);
        else if (kind == "assets")
            ok = edit(next.assets);
        else if (kind == "presets")
            ok = edit(next.presets);
        else if (kind == "roadNodes")
            ok = edit(next.roadNodes);
        else if (kind == "roadEdges")
            ok = edit(next.roadEdges);
        else if (kind == "caveMarkers")
            ok = edit(next.caveMarkers);
        else if (kind == "pcgAssets")
            ok = edit(next.pcgAssets);
        else
            error = "对象类型无效";
        if (!ok) {
            emit failure(error);
            return false;
        }
        if (action == "remove") {
            next.assetFolders.remove(id);
            if (kind == "graphs")
                std::erase_if(next.volumes, [&](auto &v) { return v.graphId == id; });
            if (kind == "roadNodes")
                std::erase_if(next.roadEdges, [&](auto &e) { return e.from == id || e.to == id; });
            if (kind == "assets") {
                std::erase_if(next.presets, [&](auto &v) { return v.assetId == id; });
                std::erase_if(next.caveMarkers, [&](auto &v) { return v.assetId == id; });
            }
        }
    } else {
        emit failure("未知创作命令");
        return false;
    }
    return commitCreation(std::move(next), "编辑创作对象");
}
bool Editor::splitMixedGraph(const QString &id) {
    auto next = m_world.creation;
    const auto *source = wb::libraryGraph(next, id);
    if (!source)
        return false;
    const auto sourceId = source->id;
    auto &scatter = next.graphs[size_t(source - next.graphs.data())];
    auto material = scatter;
    auto separate = [](wb::PcgGraph &graph, const QString &remove) {
        QSet<QString> removed;
        std::erase_if(graph.nodes, [&](const auto &n) {
            if (n.type != remove)
                return false;
            removed.insert(n.id);
            return true;
        });
        std::erase_if(graph.connections, [&](const auto &c) { return removed.contains(c.toNode); });
        graph.version = 2;
        return !removed.empty();
    };
    if (!separate(scatter, "output_surface") || !separate(material, "output_scatter"))
        return false;
    scatter.surfaceMaterial = false;
    material.surfaceMaterial = true;
    material.id = fresh("material");
    material.name = material.name.left(116) + " · 地表材质";
    next.graphs.push_back(material);
    if (next.assetFolders.contains(sourceId))
        next.assetFolders[material.id] = next.assetFolders.value(sourceId);
    // Preserve order, volume identity for the scatter pass and the material random seed.
    for (size_t i = 0; i < next.volumes.size(); ++i) {
        auto volume = next.volumes[i];
        if (volume.graphId != sourceId)
            continue;
        if (volume.seedDomain.isEmpty())
            volume.seedDomain = volume.id;
        volume.graphId = material.id;
        volume.id = fresh("material-range");
        volume.name += " · 地表材质";
        next.volumes.insert(next.volumes.begin() + i + 1, volume);
        ++i;
    }
    const auto presets = next.pcgAssets;
    for (auto asset : presets) {
        if (asset.volume.graphId != sourceId)
            continue;
        const auto oldId = asset.id;
        asset.id = fresh("material-preset");
        asset.name = asset.name.left(114) + " · 地表材质";
        asset.volume.graphId = material.id;
        if (next.assetFolders.contains(oldId))
            next.assetFolders[asset.id] = next.assetFolders.value(oldId);
        next.pcgAssets.push_back(std::move(asset));
    }
    return commitCreation(std::move(next), "拆分实例 PCG 和地表材质");
}

bool Editor::createPcgGraph(const QString &name, bool surfaceMaterial) {
    auto next = m_world.creation;
    wb::PcgGraph graph;
    graph.id = fresh("pcg");
    graph.surfaceMaterial = surfaceMaterial;
    graph.version = 2;
    graph.name = name.isEmpty() ? (surfaceMaterial ? "地表材质" : "实例 PCG 图") : name.left(128);
    graph.nodes = {{"constant", "constant", {{"value", 1}, {"uiX", 40}, {"uiY", 80}}},
                   {"surface",
                    "output_surface",
                    {{"block", "minecraft:stone"},
                     {"under", "minecraft:dirt"},
                     {"depth", 3},
                     {"uiX", 350},
                     {"uiY", 80}}}};
    graph.connections = {{"constant", "value", "surface", "value"}};
    if (!surfaceMaterial) {
        graph.nodes.back() = {
            "scatter", "output_scatter", {{"spacing", 12}, {"density", .5}, {"uiX", 350}, {"uiY", 80}}};
        graph.connections.front().toNode = "scatter";
    }
    next.graphs.push_back(graph);
    return commitCreation(std::move(next), "新增PCG图");
}
bool Editor::addPcgVolume(const QString &graphId) {
    const auto id =
        graphId.isEmpty() && !m_world.creation.graphs.empty() ? m_world.creation.graphs.front().id : graphId;
    return placeGraphVolume(id, {0, float(m_world.height(0, 0)), 0});
}
bool Editor::placeGraphVolume(const QString &graphId, QVector3D center) {
    auto next = m_world.creation;
    if (std::none_of(next.graphs.begin(), next.graphs.end(), [&](const auto &g) { return g.id == graphId; }))
        return false;
    wb::PcgVolume v;
    v.id = fresh("pcg-volume");
    v.graphId = graphId;
    for (const auto &g : next.graphs)
        if (g.id == graphId)
            v.name = instanceName(g.name, next);
    v.min = center - QVector3D(32, 32, 32);
    v.max = center + QVector3D(32, 32, 32);
    next.volumes.push_back(v);
    if (!commitCreation(std::move(next), "新增PCG范围"))
        return false;
    setSelectedNode(v.id);
    return true;
}
bool Editor::connectPcgNodes(const QString &graphId, const QString &fromNode, const QString &fromPort,
                             const QString &toNode, const QString &toPort) {
    auto g = QJsonObject::fromVariantMap(graphData(graphId));
    if (g.isEmpty())
        return false;
    auto c = g["connections"].toArray();
    for (int i = c.size() - 1; i >= 0; --i)
        if (c[i].toObject()["toNode"].toString() == toNode && c[i].toObject()["toPort"].toString() == toPort)
            c.removeAt(i);
    c.append(
        QJsonObject{{"fromNode", fromNode}, {"fromPort", fromPort}, {"toNode", toNode}, {"toPort", toPort}});
    g["connections"] = c;
    return creationEdit("graph", g.toVariantMap());
}
namespace {
bool stageLibraryFile(const QString &path, wb::CreationState &next, QString &resultId, QString &error,
                      bool forceGraph = false) {
    if (forceGraph || QFileInfo(path).suffix().compare("wbpcg", Qt::CaseInsensitive) == 0) {
        QFile file(path);
        if (file.size() > ExchangeLimit || !file.open(QIODevice::ReadOnly)) {
            error = "图文件不可读或超过64MiB";
            return false;
        }
        return loadIndexedGraph(next, QJsonDocument::fromJson(file.readAll()).object(), path, resultId,
                                error);
    }
    wb::StructureAsset asset;
    if (QFileInfo(path).suffix().compare("wbasset", Qt::CaseInsensitive) == 0) {
        QFile file(path);
        if (file.size() > 64 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
            error = "资产不可读或超过64MiB";
            return false;
        }
        const auto data = QJsonDocument::fromJson(file.readAll()).object();
        if (data["type"] == "pcg-volume") {
            wb::PcgAsset pcg;
            if (!wb::PcgAsset::fromJson(data, pcg, error)) {
                return false;
            }
            auto &graphId = pcg.volume.graphId;
            if (data["version"].toInt() == 1) {
                if (!wb::indexGraph(next, data, graphId, error)) {
                    return false;
                }
            } else if (!wb::libraryGraph(next, graphId)) {
                const auto expected = graphId;
                QJsonObject manifest;
                if (!readRecord(path, data["graphRecord"].toString(), manifest, error) ||
                    manifest["graph"].toObject()["id"] != expected ||
                    !loadIndexedGraph(next, manifest, path, graphId, error)) {
                    if (error.isEmpty())
                        error = "范围预设的图引用无效";
                    return false;
                }
            }
            pcg.id = fresh("pcg-asset");
            next.pcgAssets.push_back(pcg);
            resultId = pcg.id;
            return true;
        }
    }
    if (!wb::importStructure(path, asset, error)) {
        return false;
    }
    if (asset.id.isEmpty() || asset.id == "imported-asset" ||
        std::any_of(next.assets.begin(), next.assets.end(), [&](const auto &a) { return a.id == asset.id; }))
        asset.id = fresh("asset");
    if (QFileInfo(path).suffix().compare("wbasset", Qt::CaseInsensitive) == 0) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            auto object = QJsonDocument::fromJson(f.readAll()).object();
            auto preset = object["instance"].toObject();
            if (!preset.isEmpty()) {
                wb::AssetPreset p;
                if (!wb::AssetPreset::fromJson(preset, p, error)) {
                    return false;
                }
                if (asset.defaultRotation != p.rotation || asset.defaultScale != p.scale)
                    asset.id = fresh("asset");
                asset.defaultRotation = p.rotation;
                asset.defaultScale = p.scale;
            }
            for (auto value : object["caveMarkers"].toArray()) {
                wb::CaveMarker m;
                if (!wb::CaveMarker::fromJson(value.toObject(), m, error)) {
                    return false;
                }
                m.id = fresh("cave-marker");
                m.assetId = asset.id;
                next.caveMarkers.push_back(m);
            }
        }
    }
    next.assets.push_back(asset);
    resultId = asset.id;
    return true;
}
} // namespace
bool Editor::importStructureAsset(const QUrl &url) {
    if (!url.isLocalFile() || commandBusy() || exportBusy())
        return false;
    auto next = m_world.creation;
    QString id, error;
    if (!stageLibraryFile(url.toLocalFile(), next, id, error)) {
        emit failure(error);
        return false;
    }
    return commitCreation(std::move(next), "导入库资产");
}
QString Editor::importLibraryGraph(const QUrl &url) {
    if (!url.isLocalFile() || commandBusy() || exportBusy())
        return {};
    auto next = m_world.creation;
    QString graphId, error;
    if (!stageLibraryFile(url.toLocalFile(), next, graphId, error, true)) {
        emit failure(error);
        return {};
    }
    return commitCreation(std::move(next), "导入PCG图") ? graphId : QString{};
}
bool Editor::importLibraryDrop(const QList<QUrl> &urls, const QString &folder) {
    if (commandBusy() || exportBusy() || urls.empty() ||
        (!folder.isEmpty() && !m_world.creation.folders.contains(folder)))
        return false;
    auto next = m_world.creation;
    auto terrain = m_world.heights.instances;
    const auto world = m_world;
    const auto revision = m_revision;
    QStringList issues;
    int imported = 0, skipped = 0;
    QFutureWatcher<void> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    m_heightmapLoading = true;
    m_status = "正在读取拖入的资产…";
    emit changed();
    watcher.setFuture(QtConcurrent::run([&] {
        struct Entry {
            QFileInfo file;
            QString folder;
            int order;
        };
        QList<Entry> files;
        QSet<QString> seen;
        auto collect = [&](auto &&visit, const QFileInfo &info, QString parent) -> void {
            if (!info.exists() || info.isSymLink() || info.isJunction() ||
                seen.contains(info.canonicalFilePath()))
                return;
            seen.insert(info.canonicalFilePath());
            if (info.isDir()) {
                if (info.fileName().compare("Worldbench.dependencies", Qt::CaseInsensitive) == 0)
                    return;
                auto path = parent.isEmpty() ? info.fileName() : parent + "/" + info.fileName();
                if (path.isEmpty() || path.size() > 256) {
                    ++skipped;
                    return;
                }
                int existing = next.folders.indexOf(path);
                for (const auto &f : next.folders)
                    if (path.compare(f, Qt::CaseInsensitive) == 0) {
                        path = f;
                        existing = 0;
                        break;
                    }
                if (existing < 0)
                    next.folders.append(path);
                for (const auto &child : QDir(info.absoluteFilePath())
                                             .entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
                    visit(visit, child, path);
                return;
            }
            const auto ext = info.suffix().toLower();
            if (!QStringList{"wbpcg", "wbasset", "nbt", "schem", "schematic", "json", "png"}.contains(ext)) {
                ++skipped;
                return;
            }
            // A standalone structure in a mixed drop must enter the index before its consumers.
            int order = ext == "wbpcg" ? 1 : 0;
            if (ext == "wbasset") {
                QFile f(info.absoluteFilePath());
                if (f.size() <= ExchangeLimit && f.open(QIODevice::ReadOnly) &&
                    QJsonDocument::fromJson(f.readAll()).object()["type"] == "pcg-volume")
                    order = 2;
            }
            files.append({info, parent, order});
        };
        for (const auto &url : urls)
            if (url.isLocalFile())
                collect(collect, QFileInfo(url.toLocalFile()), folder);
        std::stable_sort(files.begin(), files.end(),
                         [](const Entry &a, const Entry &b) { return a.order < b.order; });
        for (const auto &entry : files) {
            QString error, id;
            if (entry.file.suffix().compare("png", Qt::CaseInsensitive) == 0) {
                if (auto image = wb::loadHeightmap(entry.file.absoluteFilePath(), error)) {
                    for (const auto &t : terrain)
                        if (t.image && *t.image == *image) {
                            image = t.image;
                            break;
                        }
                    terrain.push_back(importedTerrain(world, entry.file.fileName(), image));
                    if (wb::validateTerrain(terrain, error))
                        id = terrain.back().id;
                    else
                        terrain.pop_back();
                }
            } else {
                auto staged = next;
                if (stageLibraryFile(entry.file.absoluteFilePath(), staged, id, error) &&
                    wb::validateCreation(staged, error))
                    next = std::move(staged);
                else
                    id.clear();
            }
            if (id.isEmpty()) {
                ++skipped;
                if (issues.size() < 8)
                    issues.append(entry.file.fileName() + " · " + error);
            } else {
                if (!entry.folder.isEmpty())
                    next.assetFolders[id] = entry.folder;
                ++imported;
            }
        }
        next.folders.sort(Qt::CaseInsensitive);
    }));
    loop.exec();
    m_heightmapLoading = false;
    emit changed();
    if (revision != m_revision) {
        emit failure("导入期间工程已变化，请重新拖入");
        return false;
    }
    if (!commitCreation(std::move(next), "拖入库资产", &terrain))
        return false;
    m_status = QString("资产库导入 %1 项，跳过 %2 项").arg(imported).arg(skipped);
    emit changed();
    if (!issues.empty())
        emit failure(m_status + "\n" + issues.join('\n'));
    return true;
}
bool Editor::saveAssetPreset(const QString &id, const QUrl &url) {
    return creationFile("save_asset", id, url);
}
bool Editor::placeAssetInstance(const QString &id, int x, int y, int z) {
    if (commandBusy() || exportBusy())
        return false;
    for (const auto &g : m_world.creation.graphs)
        if (g.id == id)
            return placeGraphVolume(id, QVector3D(x, y, z));
    for (auto t : m_world.heights.instances)
        if (t.id == id) {
            t.id = fresh("heightmap");
            t.x = x - (t.width - 1) / 2;
            t.z = z - (t.depth - 1) / 2;
            auto instances = m_world.heights.instances;
            instances.push_back(t);
            commitTerrain(std::move(instances), "放置地貌副本");
            if (m_world.heights.instances.back().id != t.id)
                return false;
            setSelectedNode(t.id);
            return true;
        }
    auto next = m_world.creation;
    for (const auto &asset : m_world.creation.pcgAssets)
        if (asset.id == id) {
            auto volume = asset.volume;
            volume.id = fresh("pcg-volume");
            volume.name = instanceName(asset.name, next);
            volume.min += QVector3D(x, y, z);
            volume.max += QVector3D(x, y, z);
            next.volumes.push_back(volume);
            if (!commitCreation(std::move(next), "放置PCG范围资产"))
                return false;
            setSelectedNode(volume.id);
            return true;
        }
    wb::AssetPreset p;
    p.id = fresh("asset-instance");
    p.assetId = id;
    p.position = {float(x), float(y), float(z)};
    for (auto &a : next.assets)
        if (a.id == id) {
            p.name = instanceName(a.name, next);
            p.rotation = a.defaultRotation;
            p.scale = a.defaultScale;
        }
    next.presets.push_back(p);
    if (!commitCreation(std::move(next), "放置资产实例"))
        return false;
    setSelectedNode(p.id);
    return true;
}
bool Editor::addRoadNode(int x, int y, int z) {
    auto next = m_world.creation;
    wb::RoadNode n{fresh("road-node"),
                   "道路节点 " + QString::number(next.roadNodes.size() + 1),
                   {float(x), float(y), float(z)},
                   true};
    next.roadNodes.push_back(n);
    if (!commitCreation(std::move(next), "新增道路节点"))
        return false;
    setSelectedNode(n.id);
    return true;
}
bool Editor::connectRoadNodes(const QString &from, const QString &to, int width) {
    auto next = m_world.creation;
    next.roadEdges.push_back({fresh("road-edge"), from, to, width, "minecraft:gravel", false, false, true});
    return commitCreation(std::move(next), "连接道路节点");
}
bool Editor::addCaveMarker(const QString &id, const QString &kind, int x, int y, int z) {
    auto next = m_world.creation;
    wb::CaveMarker m;
    m.id = fresh("cave-marker");
    m.assetId = id;
    m.kind = kind;
    m.position = m.end = {float(x), float(y), float(z)};
    next.caveMarkers.push_back(m);
    return commitCreation(std::move(next), "新增洞穴标记");
}
QVariantMap Editor::evaluatePcg(const QString &id, double x, double z) const {
    for (auto &g : m_world.creation.graphs)
        if (g.id == id) {
            QString error;
            auto value = g.evaluate(x, z, m_world.height(int(x), int(z)), 0, m_world.seed, &error);
            return {{"ok", error.isEmpty()}, {"value", value}, {"error", error}};
        }
    return {{"ok", false}, {"error", "图不存在"}};
}
bool Editor::placeAt(int x, int y, int z) {
    if (m_roadPlacement)
        return addRoadNode(x, y, z);
    if (m_placementAsset.isEmpty())
        return false;
    bool ok = placeAssetInstance(m_placementAsset, x, y + 1, z);
    if (ok) {
        m_placementAsset.clear();
        emit changed();
    }
    return ok;
}
bool Editor::creationFile(const QString &action, const QString &id, const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    QString path = url.toLocalFile();
    if (action == "load_asset")
        return importStructureAsset(url);
    if (action == "load_graph")
        return !importLibraryGraph(url).isEmpty();
    QJsonObject data;
    if (action == "save_graph") {
        const auto *graph = wb::libraryGraph(m_world.creation, id);
        QString error;
        if (graph && !graphManifest(m_world.creation, *graph, path, data, error)) {
            emit failure(error);
            return false;
        }
    }
    if (action == "save_asset") {
        for (const auto &asset : m_world.creation.pcgAssets)
            if (asset.id == id) {
                const auto *graph = wb::libraryGraph(m_world.creation, id);
                QJsonObject manifest;
                QString hash, error;
                if (!graph || !graphManifest(m_world.creation, *graph, path, manifest, error) ||
                    !saveRecord(path, manifest, hash, error)) {
                    emit failure(error);
                    return false;
                }
                data = asset.toJson();
                data["graphRecord"] = hash;
            }
        QString assetId = id;
        QJsonObject instance;
        for (auto &p : m_world.creation.presets)
            if (p.id == id) {
                assetId = p.assetId;
                instance = p.toJson();
            }
        for (auto &a : m_world.creation.assets)
            if (a.id == assetId)
                data = QJsonObject{{"version", 1}, {"asset", a.toJson()}, {"instance", instance}};
        QJsonArray markers;
        for (auto &m : m_world.creation.caveMarkers)
            if (m.assetId == assetId)
                markers.append(m.toJson());
        if (data.contains("asset"))
            data["caveMarkers"] = markers;
    }
    if (data.isEmpty()) {
        emit failure("没有可保存的图或资产");
        return false;
    }
    QSaveFile file(path);
    auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (bytes.size() > 64 * 1024 * 1024) {
        emit failure("便携资产/图超过64MiB交换预算，请缩小资产");
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        emit failure(file.errorString());
        return false;
    }
    return true;
}

void Editor::beginCreationTransform() {
    if (!m_creationStart && !commandBusy())
        m_creationStart = std::make_shared<const wb::CreationState>(m_world.creation);
}
void Editor::previewCreationTransform(const QVariantMap &changes) {
    if (!m_creationStart)
        return;
    for (const auto &value : changes)
        if (!std::isfinite(value.toDouble()))
            return;
    auto next = *m_creationStart;
    bool found = false;
    const QStringList positions{"x", "y", "z"}, scales{"scaleX", "scaleY", "scaleZ"};
    for (auto &v : next.volumes)
        if (v.id == m_selectedNode) {
            found = true;
            for (int axis = 0; axis < 3; ++axis) {
                const QString key = QStringLiteral("rotation") + QStringLiteral("XYZ")[axis];
                v.rotation[axis] += changes.value(key, 0).toFloat();
            }
            const auto center = (v.min + v.max) * .5f;
            for (int axis = 0; axis < 3; ++axis) {
                if (changes.contains(scales[axis])) {
                    const float factor = float(std::clamp(changes[scales[axis]].toDouble(), .01, 16.));
                    const float half = std::max(.5f, (v.max[axis] - v.min[axis]) * .5f * factor);
                    v.min[axis] = center[axis] - half;
                    v.max[axis] = center[axis] + half;
                }
                if (changes.contains(positions[axis])) {
                    const float offset = float(std::round(changes[positions[axis]].toDouble()));
                    v.min[axis] += offset;
                    v.max[axis] += offset;
                }
            }
        }
    for (auto &p : next.presets)
        if (p.id == m_selectedNode) {
            found = true;
            for (int axis = 0; axis < 3; ++axis) {
                p.position[axis] += float(std::round(changes.value(positions[axis], 0).toDouble()));
                if (changes.contains(scales[axis])) {
                    const float sign = p.scale[axis] < 0 ? -1.f : 1.f;
                    p.scale[axis] = sign * float(std::clamp(int(std::round(std::abs(p.scale[axis]) *
                                                                           changes[scales[axis]].toDouble())),
                                                            1, 8));
                }
            }
            p.rotation.setY(p.rotation.y() +
                            float(std::round(changes.value("rotationY", 0).toDouble() / 90) * 90));
        }
    for (auto &n : next.roadNodes)
        if (n.id == m_selectedNode) {
            found = true;
            for (int axis = 0; axis < 3; ++axis)
                n.position[axis] += float(std::round(changes.value(positions[axis], 0).toDouble()));
        }
    if (!found || next == m_world.creation)
        return;
    // A gesture updates transforms only. The renderer moves the cached mesh;
    // authoritative PCG/voxel generation is committed once on release.
    m_world.creation = std::move(next);
    emit changed();
}
void Editor::endCreationTransform(bool cancel) {
    if (!m_creationStart)
        return;
    auto before = m_creationStart;
    m_creationStart.reset();
    if (*before == m_world.creation)
        return;
    const auto next = m_world.creation;
    m_world.creation = *before;
    if (cancel || !commitCreation(next, "变换创作实例"))
        emit changed();
}

// The snapshot is computed on a worker. A guarded event loop keeps the native
// synchronous GUI/MCP command contract while allowing progress and cancellation.
// All mutating entry points see assetBusy; revision is checked again at publish.
bool Editor::prepareCreation(wb::World &candidate, QString &error, bool cancellable, bool reuseMaterials) {
    if (candidate.creation.volumes.empty() && candidate.creation.presets.empty() &&
        candidate.creation.roadEdges.empty())
        return candidate.rebuildCreation(error);
    const auto revision = m_revision;
    auto control = std::make_shared<CreationControl>();
    m_creationControl = control;
    m_creationBusy = true;
    m_creationCancellable = cancellable;
    emit changed();
    QFutureWatcher<bool> watcher;
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(80);
    connect(&timer, &QTimer::timeout, this, &Editor::changed);
    connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([&candidate, &error, control, reuseMaterials] {
        return candidate.rebuildCreation(
            error,
            [control](int percent, const QString &) {
                control->progress = percent;
                return !control->cancelled.load();
            },
            reuseMaterials);
    }));
    timer.start();
    loop.exec();
    timer.stop();
    bool ok = watcher.result();
    m_creationBusy = false;
    m_creationCancellable = false;
    m_creationControl.reset();
    emit changed();
    if (revision != m_revision) {
        error = "生成期间工程已变化，结果已丢弃";
        return false;
    }
    return ok;
}
QString Editor::pcgPreview(const QString &graphId, const QString &nodeId, const QString &volumeId) const {
    const auto *graph = wb::libraryGraph(m_world.creation, graphId);
    if (!graph)
        return {};
    wb::PcgPlan plan;
    QString error;
    if (!wb::compileGraph(*graph, plan, error))
        return {};
    int index = -1;
    for (int i = 0; i < int(plan.instructions.size()); ++i)
        if (plan.instructions[i].id == nodeId)
            index = i;
    if (index < 0)
        return {};
    wb::PcgVolume volume;
    volume.min = {-32, 0, -32};
    volume.max = {32, 320, 32};
    for (auto &v : m_world.creation.volumes)
        if ((volumeId.isEmpty() && v.graphId == graph->id) || v.id == volumeId) {
            volume = v;
            break;
        }
    // Use the same integer column fields as the authoritative generator.
    auto without = m_world.heights;
    const bool bound = !volume.sourceId.isEmpty();
    bool sourceEnabled = !bound;
    if (bound)
        for (auto &source : without.instances)
            if (source.id == volume.sourceId) {
                sourceEnabled = source.enabled;
                source.enabled = false;
            }
    auto terrainHeight = [&](int x, int z) {
        x = std::clamp(x, m_world.originX, m_world.originX + m_world.width - 1);
        z = std::clamp(z, m_world.originZ, m_world.originZ + m_world.depth - 1);
        return int(m_world.heights[m_world.column(x, z)]);
    };
    const auto footprint = volume.footprint();
    const auto inverse = volume.orientation().inverted();
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(Qt::black);
    for (int z = 0; z < 128; ++z)
        for (int x = 0; x < 128; ++x) {
            int wx = int(std::floor(footprint.x() + (x + .5) * footprint.width() / 128)),
                wz = int(std::floor(footprint.y() + (z + .5) * footprint.height() / 128));
            if (!m_world.contains(wx, 0, wz) || !sourceEnabled || !volume.visible || !volume.enabled)
                continue;
            int h = m_world.height(wx, wz), column = m_world.column(wx, wz);
            auto local = inverse.map(QVector3D(wx + .5f, h, wz + .5f));
            if (local.x() < volume.min.x() || local.x() >= volume.max.x() || local.y() < volume.min.y() ||
                local.y() >= volume.max.y() || local.z() < volume.min.z() || local.z() >= volume.max.z())
                continue;
            double mask =
                volume.transition > 0
                    ? std::clamp(
                          std::min({local.x() - double(volume.min.x()), double(volume.max.x()) - local.x(),
                                    local.z() - double(volume.min.z()), double(volume.max.z()) - local.z()}) /
                              volume.transition,
                          0., 1.)
                    : 1.;
            mask = mask * mask * (3 - 2 * mask);
            if (bound && volume.contributionOnly)
                mask *=
                    std::clamp(double(std::abs(int(m_world.heights[column]) - int(without[column]))), 0., 1.);
            double slope =
                std::atan(std::hypot((terrainHeight(wx + 1, wz) - terrainHeight(wx - 1, wz)) * .5,
                                     (terrainHeight(wx, wz + 1) - terrainHeight(wx, wz - 1)) * .5)) *
                180 / 3.141592653589793;
            double v =
                plan.evaluateValues({double(wx), double(wz), double(h), double(m_world.heights.base(column)),
                                     slope, local.x() - .5 - volume.min.x(), local.z() - .5 - volume.min.z(),
                                     mask, volume.seed, 1, &volume.overrides})[index];
            int c = int(std::clamp(v, 0., 1.) * 255);
            image.setPixel(x, z, qRgb(c, c, c));
        }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}
QVariantMap Editor::assetSlice(const QString &id, int y, bool classified) const {
    for (auto &a : m_world.creation.assets)
        if (a.id == id) {
            QVariantList cells;
            for (auto &b : a.blocks)
                if (b.y == y)
                    cells << QVariant(QVariantList{b.x, b.z});
            QVariantList labels;
            if (classified) {
                auto r = wb::analyzeAssetCave(a, m_world.creation.caveMarkers);
                if (y >= 0 && y < a.height)
                    for (int z = 0; z < a.depth; ++z)
                        for (int x = 0; x < a.width; ++x) {
                            int label = r.labels.empty() ? 0 : r.labels[(y * a.depth + z) * a.width + x];
                            if (label)
                                labels << QVariant(QVariantList{x, z, label});
                        }
            }
            return {{"blocks", cells}, {"labels", labels}};
        }
    return {};
}

Editor *Editor::assetInspector() {
    if (!m_assetInspector) {
        m_assetInspector = std::make_unique<Editor>();
        m_assetInspector->m_inspectionOnly = true;
        m_assetInspector->m_tool = -1;
    }
    return m_assetInspector.get();
}
bool Editor::showAssetPreview(const QString &id) {
    auto found = std::find_if(m_world.creation.assets.begin(), m_world.creation.assets.end(),
                              [&](auto &a) { return a.id == id; });
    auto *inspector = assetInspector();
    if (found == m_world.creation.assets.end()) {
        const auto &terrains = m_world.heights.instances;
        auto terrain =
            std::find_if(terrains.begin(), terrains.end(), [&](const auto &t) { return t.id == id; });
        if (terrain == terrains.end())
            return false;
        if (inspector->m_inspectionId == id && !inspector->m_world.heights.instances.empty() &&
            inspector->m_world.heights.instances.front() == *terrain)
            return true;
        wb::WorldOptions options;
        options.width = options.depth = 32;
        options.flat = true;
        options.seaLevel = -64;
        auto world = wb::World::create(options);
        world.heights.instances.push_back(*terrain);
        world.environment = inspector->m_world.environment;
        const bool reframe = inspector->m_inspectionId != id;
        inspector->m_world = std::move(world);
        inspector->m_inspectionId = id;
        ++inspector->m_revision;
        ++inspector->m_epoch;
        inspector->dirtyAll();
        emit inspector->changed();
        if (reframe)
            emit inspector->worldReset();
        return true;
    }
    if (inspector->m_world.assets == m_world.assets && !inspector->m_world.creation.assets.empty() &&
        inspector->m_world.creation.assets.front().blocks == found->blocks && inspector->m_inspectionId == id)
        return true;
    auto asset = *found;
    asset.placement = "place";
    asset.groundBlend = 0;
    asset.entities.clear();
    asset.pivot = {float(asset.width / 2), 0, float(asset.depth / 2)};
    wb::WorldOptions options;
    options.width = std::max(32, ((asset.width + 31) / 32) * 32);
    options.depth = std::max(32, ((asset.depth + 31) / 32) * 32);
    options.flat = true;
    options.baseHeight = -63;
    options.seaLevel = -64;
    auto world = wb::World::create(options);
    world.setAssets(m_world.assets);
    world.creation.assets.push_back(asset);
    wb::AssetPreset p;
    p.id = "asset-preview";
    p.assetId = asset.id;
    p.position = {0, -63, 0};
    world.creation.presets.push_back(p);
    QString error;
    if (!world.rebuildCreation(error)) {
        emit failure(error);
        return false;
    }
    world.environment = inspector->m_world.environment;
    inspector->m_world = std::move(world);
    inspector->m_inspectionId = id;
    ++inspector->m_revision;
    ++inspector->m_epoch;
    inspector->dirtyAll();
    emit inspector->changed();
    emit inspector->worldReset();
    return true;
}
