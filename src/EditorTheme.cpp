#include "Editor.h"
#include <QJsonObject>
#include <algorithm>
QVariantMap Editor::surfaceTheme() const {
    return wb::saveTheme(m_world.theme).toVariantMap();
}
QString Editor::themeMaterialLabel(int id) const {
    if (id < 0 || id >= int(m_world.palette.size()))
        return "未选择";
    const auto &state = m_world.palette[id];
    if (m_world.assets && m_world.assets->catalog.contains(state.name))
        return m_world.assets->catalog[state.name].title;
    return id < wb::BlockCount ? QString::fromUtf8(wb::info(id).name) : state.name;
}
int Editor::resolveThemeMaterial(const QString &name) {
    if (commandBusy() || exportBusy())
        return -1;
    wb::BlockState state;
    std::shared_ptr<const wb::BlockModel> model;
    for (int i = 0; i < wb::BlockCount; ++i)
        if (m_world.palette[i].name == name) {
            state = m_world.palette[i];
            model = m_world.models[i];
            break;
        }
    if (m_world.assets && m_world.assets->catalog.contains(name)) {
        const auto &entry = m_world.assets->catalog[name];
        state = entry.defaults;
        model = entry.preview;
    }
    if (!model || !model->full || model->translucent || model->fallback || model->faces.empty()) {
        emit failure(
            "地表主题当前只支持完整、不透明且有模型的方块；半砖、流体和专用渲染方块请使用涂绘/放置工具");
        return -1;
    }
    try {
        return int(m_world.intern(state));
    } catch (const std::exception &e) {
        emit failure(QString::fromUtf8(e.what()));
        return -1;
    }
}
bool Editor::applySurfaceTheme(const QVariantMap &values) {
    if (commandBusy() || exportBusy())
        return false;
    wb::SurfaceTheme next;
    QString error;
    if (!wb::loadTheme(QJsonObject::fromVariantMap(values), next, m_world.palette.size(), error)) {
        emit failure(error);
        return false;
    }
    if (next.enabled)
        for (const auto &band : next.bands)
            for (auto id : {band.surface, band.subsurface}) {
                const auto &model = *m_world.models[id];
                if (!model.full || model.translucent || model.fallback || model.faces.empty()) {
                    emit failure("主题材料缺少完整不透明模型，请更换材料或重新载入对应资源：" +
                                 m_world.palette[id].name);
                    return false;
                }
            }
    if (next == m_world.theme)
        return true;
    wb::Command command;
    command.label = "地表高度主题";
    command.themeBefore = std::make_shared<const wb::SurfaceTheme>(m_world.theme);
    command.themeAfter = std::make_shared<const wb::SurfaceTheme>(std::move(next));
    m_world.apply(command, true);
    ++m_revision;
    if (!dirtyCommand(command))
        return false;
    pushCommand(std::move(command));
    m_status = "已应用地表主题 · 高度变化会重新匹配材料 · Ctrl+Z 撤销";
    emit changed();
    return true;
}
