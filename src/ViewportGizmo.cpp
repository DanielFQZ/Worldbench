#include "Viewport.h"
#include <QGuiApplication>
#include <QLineF>
#include <QPolygonF>
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
    if (m_gizmoDragging)
        center = m_gizmoCenter;
    const double length =
        m_gizmoDragging
            ? m_gizmoWorldLength
            : std::clamp(double((center - m_camera).length()) * 90 / std::max(1., height()) * 1.1547, 2.,
                         150.);
    const auto origin = projectPoint(center);
    result["cx"] = origin.x();
    result["cy"] = origin.y();
    result["length"] = length;
    result["active"] = true;
    result["dragging"] = m_gizmoDragging;
    result["readout"] = m_gizmoReadout;
    result["dragX"] = m_gizmoLast.x();
    result["dragY"] = m_gizmoLast.y();
    result["hover"] = m_gizmoHover;
    result["axis"] = m_gizmoAxis;
    result["angle"] = m_gizmoAngle;
    QVariantList planes;
    if (m_gizmoMode == 0) {
        const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
        const QVector3D vectors[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int n = 0; n < 3; ++n) {
            QVariantList points;
            for (auto pair : {QPointF(.22, .22), QPointF(.43, .22), QPointF(.43, .43), QPointF(.22, .43)}) {
                const auto p = projectPoint(
                    center + (vectors[pairs[n][0]] * pair.x() + vectors[pairs[n][1]] * pair.y()) * length);
                points.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
            }
            planes.append(QVariantMap{
                {"axis", n + 4}, {"points", points}, {"label", QStringList{"XY", "XZ", "YZ"}[n]}});
        }
    }
    result["planes"] = planes;
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
                                {"selected", m_gizmoDragging ? m_gizmoAxis == k : m_gizmoHover == k}});
    }
    result["axes"] = axes;
    return result;
}
int Viewport::hitGizmo(QPointF p, const QVariantMap &overlay) const {
    if (!overlay["active"].toBool())
        return -1;
    QPointF center(overlay["cx"].toDouble(), overlay["cy"].toDouble());
    if (m_gizmoMode != 1 && QLineF(p, center).length() < 9)
        return 3;
    for (auto item : overlay["planes"].toList()) {
        auto plane = item.toMap();
        QPolygonF polygon;
        for (auto v : plane["points"].toList()) {
            auto a = v.toMap();
            polygon << QPointF(a["x"].toDouble(), a["y"].toDouble());
        }
        if (polygon.containsPoint(p, Qt::OddEvenFill))
            return plane["axis"].toInt();
    }
    int axis = -1;
    double best = 9;
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
    return axis;
}
bool Viewport::pressGizmo(QPointF p) {
    if (!m_editor || m_editor->assetBusy() || m_editor->terrainEditing())
        return false;
    auto overlay = gizmoOverlay();
    for (auto item : overlay["markers"].toList()) {
        auto m = item.toMap();
        if (QLineF(p, {m["x"].toDouble(), m["y"].toDouble() - 24}).length() < 14) {
            m_editor->setSelectedNode(m["id"].toString());
            m_editor->setTool(-1);
            return true;
        }
    }
    int axis = hitGizmo(p, overlay);
    if (axis < 0)
        return false;
    for (const auto &i : m_editor->world().heights.instances)
        if (i.id == m_editor->selectedNode())
            m_gizmoInitial = i;
    const auto pivot = m_gizmoInitial.pivot();
    m_gizmoCenter = {pivot.x(), float(m_editor->world().height(int(pivot.x()), int(pivot.z())) + 7),
                     pivot.z()};
    m_gizmoAxis = axis;
    m_gizmoStart = m_gizmoLast = p;
    m_gizmoScreenCenter = {overlay["cx"].toDouble(), overlay["cy"].toDouble()};
    m_gizmoWorldLength = overlay["length"].toDouble();
    m_gizmoAngle = 0;
    m_gizmoReadout.clear();
    const QVector3D basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (axis < 3 && m_gizmoMode != 1)
        m_gizmoScreenAxis =
            projectPoint(m_gizmoCenter + basis[axis] * m_gizmoWorldLength) - m_gizmoScreenCenter;
    if (axis >= 4) {
        const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
        m_gizmoPlaneU = projectPoint(m_gizmoCenter + basis[pairs[axis - 4][0]] * m_gizmoWorldLength) -
                        m_gizmoScreenCenter;
        m_gizmoPlaneV = projectPoint(m_gizmoCenter + basis[pairs[axis - 4][1]] * m_gizmoWorldLength) -
                        m_gizmoScreenCenter;
    }
    m_editor->beginTerrainEdit();
    m_gizmoDragging = true;
    moveGizmo(p);
    emit cameraChanged();
    return true;
}
void Viewport::moveGizmo(QPointF p) {
    if (!m_gizmoDragging)
        return;
    const auto d = p - m_gizmoStart;
    const auto &i = m_gizmoInitial;
    QVariantMap changes;
    double projected = QPointF::dotProduct(m_gizmoScreenAxis, m_gizmoScreenAxis);
    double t = projected > 25 ? QPointF::dotProduct(d, m_gizmoScreenAxis) / projected : d.x() / 90.;
    const bool snap = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    auto quantize = [snap](double value, int step) {
        return int(std::lround(value / (snap ? step : 1))) * (snap ? step : 1);
    };
    if (m_gizmoMode == 0) {
        if (m_gizmoAxis >= 4) {
            double det = m_gizmoPlaneU.x() * m_gizmoPlaneV.y() - m_gizmoPlaneU.y() * m_gizmoPlaneV.x();
            if (std::abs(det) < 1)
                return;
            double u = (d.x() * m_gizmoPlaneV.y() - d.y() * m_gizmoPlaneV.x()) / det * m_gizmoWorldLength;
            double v = (m_gizmoPlaneU.x() * d.y() - m_gizmoPlaneU.y() * d.x()) / det * m_gizmoWorldLength;
            const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
            const QString keys[3] = {"x", "y", "z"};
            const int start[3] = {i.x, i.y, i.z};
            for (int n = 0; n < 2; ++n) {
                int axis = pairs[m_gizmoAxis - 4][n], limit = axis == 1 ? 10000 : 2000000;
                changes[keys[axis]] = std::clamp(start[axis] + quantize(n ? v : u, 4), -limit, limit);
            }
        } else if (m_gizmoAxis == 3) {
            const auto r = right();
            const double units = m_gizmoWorldLength / 90.;
            auto forwardFlat = QVector3D(forward().x(), 0, forward().z()).normalized();
            auto move = (r * float(d.x()) - forwardFlat * float(d.y())) * units;
            changes = {{"x", std::clamp(i.x + quantize(move.x(), 4), -2000000, 2000000)},
                       {"z", std::clamp(i.z + quantize(move.z(), 4), -2000000, 2000000)}};
        } else {
            const QString keys[3] = {"x", "y", "z"};
            int starts[3] = {i.x, i.y, i.z};
            int limit = m_gizmoAxis == 1 ? 10000 : 2000000;
            changes[keys[m_gizmoAxis]] =
                std::clamp(starts[m_gizmoAxis] + quantize(t * m_gizmoWorldLength, 4), -limit, limit);
        }
    } else if (m_gizmoMode == 1) {
        auto a = m_gizmoLast - m_gizmoScreenCenter, b = p - m_gizmoScreenCenter;
        double angle =
            std::atan2(a.x() * b.y() - a.y() * b.x(), QPointF::dotProduct(a, b)) * 180 / 3.141592653589793;
        const int start[3] = {i.rotationX, i.rotationY, i.rotationZ};
        const QString keys[3] = {"rotationX", "rotationY", "rotationZ"};
        const QVector3D basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const double direction =
            QVector3D::dotProduct(m_camera - m_gizmoCenter, basis[m_gizmoAxis]) >= 0 ? -1 : 1;
        m_gizmoAngle += angle * direction;
        int value = start[m_gizmoAxis] + quantize(m_gizmoAngle, 15);
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
                changes[keys[k]] = std::clamp(quantize(start[k] * (1 + d.x() / 120), 10), 1, 1600);
        else
            changes[keys[m_gizmoAxis]] = std::clamp(quantize(start[m_gizmoAxis] * (1 + t), 10), 1, 1600);
    }
    m_editor->setTerrainOptions(changes);
    m_gizmoLast = p;
    const auto now = m_editor->terrainDetails();
    if (m_gizmoMode == 0)
        m_gizmoReadout = QString("位移 Δ  X %1   Y %2   Z %3 格")
                             .arg(now["x"].toInt() - i.x)
                             .arg(now["y"].toInt() - i.y)
                             .arg(now["z"].toInt() - i.z);
    else if (m_gizmoMode == 1) {
        const QString keys[3] = {"rotationX", "rotationY", "rotationZ"};
        m_gizmoReadout = QString("旋转 %1  %2°   Δ %3°")
                             .arg(QStringList{"X", "Y", "Z"}[m_gizmoAxis])
                             .arg(now[keys[m_gizmoAxis]].toInt())
                             .arg(quantize(m_gizmoAngle, 15));
    } else
        m_gizmoReadout = QString("缩放  X %1%   Y %2%   Z %3%")
                             .arg(now["scaleX"].toInt())
                             .arg(now["scaleY"].toInt())
                             .arg(now["scaleZ"].toInt());
    if (snap)
        m_gizmoReadout += "  · 吸附";
    emit cameraChanged();
}
void Viewport::finishGizmo(bool cancel) {
    if (!m_gizmoDragging)
        return;
    m_gizmoDragging = false;
    m_gizmoReadout.clear();
    m_gizmoHover = -1;
    m_editor->endTerrainEdit(cancel);
    emit cameraChanged();
}
