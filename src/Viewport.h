#pragma once
#include "Editor.h"
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QQuickRhiItem>
#include <QSet>
#include <QTimer>

class Viewport : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString cameraText READ cameraText NOTIFY cameraChanged)
    Q_PROPERTY(QString hitText READ hitText NOTIFY cameraChanged)
  public:
    explicit Viewport(QQuickItem *parent = nullptr);
    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    QString cameraText() const;
    QString hitText() const;
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void focusTarget();
    QMatrix4x4 viewProjection() const;
    QVector3D camera() const { return m_camera; }
    wb::Hit hit() const { return m_hit; }
    std::shared_ptr<std::atomic_int> frameCounter() const { return m_frames; }
  signals:
    void editorChanged();
    void cameraChanged();

  protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void hoverMoveEvent(QHoverEvent *) override;
    void hoverLeaveEvent(QHoverEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mouseUngrabEvent() override;

  private:
    QVector3D forward() const;
    QVector3D right() const;
    void pick(QPointF pos);
    void stopInput();
    void tick();
    void changedCamera();
    Editor *m_editor = nullptr;
    QVector3D m_camera{80, 65, 100}, m_pivot{0, 20, 0};
    float m_yaw = -38.66f, m_pitch = -19.36f, m_speed = 35;
    QPointF m_pointer, m_anchorGlobal;
    wb::Hit m_hit;
    QTimer m_timer;
    QElapsedTimer m_elapsed, m_stampClock;
    QSet<int> m_keys;
    Qt::MouseButtons m_buttons;
    bool m_sculpting = false, m_altNav = false, m_reverse = false;
    std::shared_ptr<std::atomic_int> m_frames = std::make_shared<std::atomic_int>(0);
};
