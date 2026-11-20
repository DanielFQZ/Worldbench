#include "Editor.h"

QVariantMap Editor::environmentDetails() const {
    return wb::saveEnvironment(m_world.environment).toVariantMap();
}
void Editor::beginEnvironmentEdit() {
    if (assetBusy() || m_environmentStart)
        return;
    endTerrainEdit();
    endStroke();
    m_environmentStart = std::make_shared<const wb::Environment>(m_world.environment);
    emit changed();
}
void Editor::endEnvironmentEdit(bool cancel) {
    if (!m_environmentStart)
        return;
    wb::Command c;
    c.label = "环境预览";
    c.environmentBefore = m_environmentStart;
    c.environmentAfter = std::make_shared<const wb::Environment>(m_world.environment);
    m_environmentStart.reset();
    if (*c.environmentBefore == *c.environmentAfter) {
        emit changed();
        return;
    }
    if (cancel) {
        m_world.apply(c, false);
        ++m_revision;
        emit changed();
    } else
        pushCommand(std::move(c));
}
bool Editor::setEnvironment(const QVariantMap &changes) {
    if (assetBusy())
        return false;
    wb::Environment next;
    QString error;
    if (!wb::patchEnvironment(m_world.environment, QJsonObject::fromVariantMap(changes), next, error)) {
        emit failure(error);
        return false;
    }
    if (next == m_world.environment)
        return true;
    endTerrainEdit();
    endStroke();
    wb::Command c;
    c.label = "环境预览";
    c.environmentBefore = std::make_shared<const wb::Environment>(m_world.environment);
    c.environmentAfter = std::make_shared<const wb::Environment>(next);
    m_world.apply(c, true);
    ++m_revision;
    if (!m_environmentStart)
        pushCommand(std::move(c));
    emit changed();
    return true;
}
