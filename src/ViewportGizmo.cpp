#include "Viewport.h"
#include <QLineF>
#include <algorithm>
#include <cmath>
namespace {
double distanceToLine(QPointF p, QPointF a, QPointF b) {
    auto d = b - a;
    double l = QPointF::dotProduct(d, d);
    return QLineF(p, a + d * std::clamp(l > 0 ? QPointF::dotProduct(p - a, d) / l : 0., 0., 1.)).length();
}
} // namespace
QPointF Viewport::projectPoint(QVector3D p, bool *visible) const {
    auto q = viewProjection() * QVector4D(p, 1);
    if (visible)
        *visible = q.w() > .15f && q.z() > -q.w() && q.z() < q.w();
    if (std::abs(q.w()) < .0001f)
        return {-10000, -10000};
    return {(q.x() / q.w() + 1) * width() * .5, (1 - q.y() / q.w()) * height() * .5};
}
void Viewport::setGizmoMode(int mode) {
    if (m_gizmoDragging)
        finishGizmo();
    m_gizmoMode = std::clamp(mode, 0, 2);
    emit cameraChanged();
}
QVariantMap Viewport::gizmoOverlay() const {
    QVariantList markers, axes;
    if (!m_editor || width() < 1 || height() < 1)
        return {};
    const auto &world = m_editor->world();
    const wb::TerrainInstance *selected = nullptr;
    QVector3D center;
    for (const auto &i : world.heights.instances) {
        const auto pivot = i.pivot();
        const int x = int(pivot.x()), z = int(pivot.z());
        if (!world.contains(x, 0, z))
            continue;
        auto position = QVector3D(pivot.x(), float(world.height(x, z) + 7), pivot.z());
        bool visible;
        auto screen = projectPoint(position, &visible);
        visible = visible && screen.x() > 16 && screen.x() < width() - 16 && screen.y() > 55 &&
                  screen.y() < height() - 65;
        if (visible)
            markers.append(QVariantMap{{"id", i.id},
                                       {"name", i.name},
                                       {"x", screen.x()},
                                       {"y", screen.y()},
                                       {"enabled", i.enabled},
                                       {"selected", i.id == m_editor->selectedNode()}});
        if (i.id == m_editor->selectedNode() && visible) {
            selected = &i;
            center = position;
        }
    }
    QVariantMap result{{"markers", markers}, {"axes", axes}, {"mode", m_gizmoMode}};
    if (!selected || m_editor->tool() != -1)
        return result;
    const double length =
        std::clamp(double((center - m_camera).length()) * 90 / std::max(1., height()) * 1.1547, 2., 150.);
    const auto origin = projectPoint(center);
    result["cx"] = origin.x();
    result["cy"] = origin.y();
    result["length"] = length;
    result["active"] = true;
    const QVector3D basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const QString colors[3] = {"#ef6666", "#76d789", "#70a9ff"};
    for (int k = 0; k < 3; ++k) {
        QVariantList points;
        if (m_gizmoMode == 1)
            for (int n = 0; n <= 64; ++n) {
                double a = n * 6.283185307179586 / 64;
                auto p = projectPoint(
                    center + (basis[(k + 1) % 3] * std::cos(a) + basis[(k + 2) % 3] * std::sin(a)) * length);
                points.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
            }
        else {
            auto p = projectPoint(center + basis[k] * length);
            points = {QVariantMap{{"x", origin.x()}, {"y", origin.y()}},
                      QVariantMap{{"x", p.x()}, {"y", p.y()}}};
        }
        axes.append(QVariantMap{{"axis", k},
                                {"color", colors[k]},
                                {"points", points},
                                {"selected", m_gizmoDragging && m_gizmoAxis == k}});
    }
    result["axes"] = axes;
    return result;
}
bool Viewport::pressGizmo(QPointF p) {
    if (!m_editor || m_editor->assetBusy() || m_editor->terrainEditing())
        return false;
    auto overlay = gizmoOverlay();
    // Icons remain selectable for hidden instances; Alt/RMB navigation bypasses this path.
    for (auto item : overlay["markers"].toList()) {
        auto m = item.toMap();
        if (QLineF(p, {m["x"].toDouble(), m["y"].toDouble() - 24}).length() < 14) {
            m_editor->setSelectedNode(m["id"].toString());
            m_editor->setTool(-1);
            return true;
        }
    }
    int axis = -1;
    double best = 9;
    if (overlay["active"].toBool()) {
        QPointF center(overlay["cx"].toDouble(), overlay["cy"].toDouble());
        if (m_gizmoMode != 1 && QLineF(p, center).length() < 9) {
            axis = 3;
            best = 0;
        }
        for (auto item : overlay["axes"].toList()) {
            auto a = item.toMap();
            auto pts = a["points"].toList();
            for (int n = 1; n < pts.size(); ++n) {
                auto x = pts[n - 1].toMap(), y = pts[n].toMap();
                double distance = distanceToLine(p, {x["x"].toDouble(), x["y"].toDouble()},
                                                 {y["x"].toDouble(), y["y"].toDouble()});
                if (distance < best) {
                    best = distance;
                    axis = a["axis"].toInt();
                }
            }
        }
        if (axis >= 0) {
            for (const auto &i : m_editor->world().heights.instances)
                if (i.id == m_editor->selectedNode())
                    m_gizmoInitial = i;
            m_gizmoAxis = axis;
            m_gizmoStart = p;
            m_gizmoScreenCenter = center;
            m_gizmoWorldLength = overlay["length"].toDouble();
            if (axis < 3 && m_gizmoMode != 1) {
                auto pts = overlay["axes"].toList()[axis].toMap()["points"].toList();
                auto end = pts.last().toMap();
                m_gizmoScreenAxis = QPointF(end["x"].toDouble(), end["y"].toDouble()) - center;
            }
            m_editor->beginTerrainEdit();
            m_gizmoDragging = true;
            emit cameraChanged();
            return true;
        }
    }
    return false;
}
void Viewport::moveGizmo(QPointF p) {
    if (!m_gizmoDragging)
        return;
    const auto d = p - m_gizmoStart;
    const auto &i = m_gizmoInitial;
    QVariantMap changes;
    double projected = QPointF::dotProduct(m_gizmoScreenAxis, m_gizmoScreenAxis);
    double t = projected > 25 ? QPointF::dotProduct(d, m_gizmoScreenAxis) / projected : d.x() / 90.;
    if (m_gizmoMode == 0) {
        if (m_gizmoAxis == 3) {
            const auto r = right();
            const double units = m_gizmoWorldLength / 90.;
            auto forwardFlat = QVector3D(forward().x(), 0, forward().z()).normalized();
            auto move = (r * float(d.x()) - forwardFlat * float(d.y())) * units;
            changes = {{"x", std::clamp(i.x + int(std::lround(move.x())), -2000000, 2000000)},
                       {"z", std::clamp(i.z + int(std::lround(move.z())), -2000000, 2000000)}};
        } else {
            const QString keys[3] = {"x", "y", "z"};
            int starts[3] = {i.x, i.y, i.z};
            int limit = m_gizmoAxis == 1 ? 10000 : 2000000;
            changes[keys[m_gizmoAxis]] =
                std::clamp(starts[m_gizmoAxis] + int(std::lround(t * m_gizmoWorldLength)), -limit, limit);
        }
    } else if (m_gizmoMode == 1) {
        auto a = m_gizmoStart - m_gizmoScreenCenter, b = p - m_gizmoScreenCenter;
        double angle =
            std::atan2(a.x() * b.y() - a.y() * b.x(), QPointF::dotProduct(a, b)) * 180 / 3.141592653589793;
        const int start[3] = {i.rotationX, i.rotationY, i.rotationZ};
        const QString keys[3] = {"rotationX", "rotationY", "rotationZ"};
        int value = start[m_gizmoAxis] + int(std::lround(angle));
        while (value > 180)
            value -= 360;
        while (value < -180)
            value += 360;
        changes[keys[m_gizmoAxis]] = value;
    } else {
        const int start[3] = {i.scaleX, i.scaleY, i.scaleZ};
        const QString keys[3] = {"scaleX", "scaleY", "scaleZ"};
        if (m_gizmoAxis == 3)
            for (int k = 0; k < 3; ++k)
                changes[keys[k]] = std::clamp(int(std::lround(start[k] * (1 + d.x() / 120))), 1, 1600);
        else
            changes[keys[m_gizmoAxis]] = std::clamp(int(std::lround(start[m_gizmoAxis] * (1 + t))), 1, 1600);
    }
    m_editor->setTerrainOptions(changes);
}
void Viewport::finishGizmo(bool cancel) {
    if (!m_gizmoDragging)
        return;
    m_gizmoDragging = false;
    m_editor->endTerrainEdit(cancel);
    emit cameraChanged();
}
