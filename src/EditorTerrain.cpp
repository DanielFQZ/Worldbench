#include "Editor.h"
#include "Viewport.h"
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
int Editor::selectedTerrainIndex() const {
    const auto &instances = m_world.heights.instances;
    for (size_t i = 0; i < instances.size(); ++i)
        if (instances[i].id == m_selectedNode)
            return int(i);
    return -1;
}
void Editor::setSelectedNode(const QString &id) {
    if (id == m_selectedNode)
        return;
    if (id != "terrain-base-1" &&
        std::none_of(m_world.heights.instances.begin(), m_world.heights.instances.end(),
                     [&](const auto &i) { return i.id == id; }))
        return;
    m_selectedNode = id;
    emit changed();
}
QVariantMap Editor::terrainDetails() const {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return {{"valid", false}};
    const auto &i = m_world.heights.instances[index];
    return {{"valid", true},
            {"id", i.id},
            {"name", i.name},
            {"source", i.sourceName},
            {"mask", i.maskName},
            {"pixelsX", i.image->width},
            {"pixelsZ", i.image->depth},
            {"x", i.x},
            {"z", i.z},
            {"width", i.width},
            {"depth", i.depth},
            {"low", i.low},
            {"high", i.high},
            {"blend", i.blend},
            {"opacity", i.opacity},
            {"feather", i.feather},
            {"enabled", i.enabled},
            {"canUp", index + 1 < int(m_world.heights.instances.size())},
            {"canDown", index > 0}};
}
void Editor::commitTerrain(std::vector<wb::TerrainInstance> next, const QString &label) {
    if (assetBusy())
        return;
    QString error;
    if (!wb::validateTerrain(next, error)) {
        emit failure(error);
        return;
    }
    if (next == m_world.heights.instances)
        return;
    endStroke();
    wb::Command command;
    command.label = label;
    command.terrainBefore =
        std::make_shared<const std::vector<wb::TerrainInstance>>(m_world.heights.instances);
    command.terrainAfter = std::make_shared<const std::vector<wb::TerrainInstance>>(std::move(next));
    m_world.apply(command, true);
    ++m_revision;
    dirtyCommand(command);
    pushCommand(std::move(command));
    m_status = label + " · Ctrl+Z 撤销";
    emit changed();
}
void Editor::initializeTerrainImporter() {
    connect(&m_heightmapWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_heightmapLoading = false;
        const auto result = m_heightmapWatcher.result();
        if (!result.image) {
            m_status = "高度图导入失败，工程未改动";
            emit failure(result.error);
            emit changed();
            return;
        }
        auto next = m_world.heights.instances;
        QString selected;
        if (result.mask) {
            auto it = std::find_if(next.begin(), next.end(),
                                   [&](const auto &i) { return i.id == result.targetId; });
            if (it == next.end()) {
                emit failure("目标地貌实例已不存在");
                emit changed();
                return;
            }
            it->mask = result.image;
            it->maskName = result.fileName;
            selected = it->id;
        } else {
            wb::TerrainInstance i;
            i.id = "heightmap-" + QUuid::createUuid().toString(QUuid::Id128);
            i.name = QFileInfo(result.fileName).completeBaseName().left(128);
            if (i.name.isEmpty())
                i.name = "高度图地貌";
            i.sourceName = result.fileName;
            i.image = result.image;
            i.width = std::min(m_world.width, result.image->width);
            i.depth = std::min(m_world.depth, result.image->depth);
            i.x = m_world.originX + (m_world.width - i.width) / 2;
            i.z = m_world.originZ + (m_world.depth - i.depth) / 2;
            i.low = m_world.height(i.x + i.width / 2, i.z + i.depth / 2);
            i.high = std::min(318, i.low + 64);
            selected = i.id;
            next.push_back(std::move(i));
        }
        QString error;
        if (!wb::validateTerrain(next, error)) {
            emit failure(error);
            m_status = error;
            emit changed();
            return;
        }
        commitTerrain(std::move(next), result.mask ? "已应用地貌蒙版" : "已导入高度图实例");
        setSelectedNode(selected);
        if (!result.mask)
            focusTerrain();
    });
}
void Editor::importHeightmap(const QUrl &url, bool mask) {
    if (assetBusy() || exportBusy())
        return;
    if (!url.isLocalFile() || (mask && selectedTerrainIndex() < 0)) {
        emit failure(mask ? "请先选择一个高度图实例" : "请选择本地灰度 PNG");
        return;
    }
    endStroke();
    m_heightmapLoading = true;
    m_status = mask ? "正在后台读取地貌蒙版…" : "正在后台读取高度图…";
    const auto path = url.toLocalFile(), targetId = m_selectedNode;
    m_heightmapWatcher.setFuture(QtConcurrent::run([path, targetId, mask] {
        HeightmapResult r;
        r.fileName = QFileInfo(path).fileName();
        r.targetId = targetId;
        r.mask = mask;
        r.image = wb::loadHeightmap(path, r.error);
        return r;
    }));
    emit changed();
}
void Editor::setTerrainOption(const QString &key, const QVariant &value) {
    const int index = selectedTerrainIndex();
    if (index < 0 || assetBusy())
        return;
    auto next = m_world.heights.instances;
    auto &i = next[index];
    if (key == "enabled")
        i.enabled = value.toBool();
    else if (key == "name")
        i.name = value.toString().trimmed();
    else {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || std::floor(number) != number || std::abs(number) > 2000000)
            return;
        const int n = int(number);
        if (key == "x")
            i.x = n;
        else if (key == "z")
            i.z = n;
        else if (key == "width")
            i.width = n;
        else if (key == "depth")
            i.depth = n;
        else if (key == "low")
            i.low = n;
        else if (key == "high")
            i.high = n;
        else if (key == "blend")
            i.blend = n;
        else if (key == "opacity")
            i.opacity = n;
        else if (key == "feather")
            i.feather = n;
        else
            return;
    }
    commitTerrain(std::move(next), "已调整地貌实例");
}
void Editor::removeTerrain() {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return;
    auto next = m_world.heights.instances;
    next.erase(next.begin() + index);
    commitTerrain(std::move(next), "已移除地貌实例");
}
void Editor::moveTerrain(int direction) {
    const int index = selectedTerrainIndex(), target = index + (direction > 0 ? 1 : -1);
    auto next = m_world.heights.instances;
    if (index < 0 || target < 0 || target >= int(next.size()))
        return;
    std::swap(next[index], next[target]);
    commitTerrain(std::move(next), "已调整地貌叠放顺序");
}
void Editor::clearTerrainMask() {
    const int index = selectedTerrainIndex();
    if (index < 0)
        return;
    auto next = m_world.heights.instances;
    next[index].mask.reset();
    next[index].maskName.clear();
    commitTerrain(std::move(next), "已移除地貌蒙版");
}
void Editor::focusTerrain() {
    const int index = selectedTerrainIndex();
    auto *viewport = qobject_cast<Viewport *>(m_cameraController.data());
    if (index < 0 || !viewport)
        return;
    const auto &i = m_world.heights.instances[index];
    viewport->jumpTo(i.x + i.width / 2, i.z + i.depth / 2);
}
