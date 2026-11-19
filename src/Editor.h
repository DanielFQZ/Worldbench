#pragma once
#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/World.h"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <set>

class Editor : public QObject {
    Q_OBJECT
    Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY changed)
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY changed)
    Q_PROPERTY(int strength READ strength WRITE setStrength NOTIFY changed)
    Q_PROPERTY(int material READ material WRITE setMaterial NOTIFY changed)
    Q_PROPERTY(bool modified READ modified NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool meshBusy READ meshBusy NOTIFY changed)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY changed)
    Q_PROPERTY(int exportProgress READ exportProgress NOTIFY changed)
    Q_PROPERTY(QString projectName READ projectName NOTIFY changed)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString sceneStats READ sceneStats NOTIFY changed)
  public:
    explicit Editor(QObject *parent = nullptr);
    ~Editor() override;
    int tool() const { return m_tool; }
    void setTool(int v);
    int radius() const { return m_radius; }
    void setRadius(int v);
    int strength() const { return m_strength; }
    void setStrength(int v);
    int material() const { return m_material; }
    void setMaterial(int v);
    bool modified() const { return m_strokeChanged || m_state != m_savedState; }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < int(m_history.size()); }
    bool meshBusy() const { return m_meshWatcher.isRunning() || !m_dirty.empty(); }
    bool exportBusy() const { return m_exportWatcher.isRunning(); }
    int exportProgress() const { return m_exportProgress; }
    QString projectName() const;
    QString projectPath() const { return m_path; }
    QString status() const { return m_status; }
    QString sceneStats() const;
    const wb::World &world() const { return m_world; }
    std::shared_ptr<const wb::MeshScene> scene() const { return m_scene; }
    void beginStroke();
    void stamp(const wb::Hit &hit, bool reverse);
    void endStroke();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void newWorld();
    Q_INVOKABLE bool save(const QUrl &url = QUrl());
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE void exportWorld(const QUrl &parent, const QString &name);
    Q_INVOKABLE void cancelExport();
  signals:
    void changed();
    void sceneChanged();
    void worldReset();
    void failure(const QString &message);
    void exported(const QString &path);

  private:
    void dirtyAll();
    void startMesh();
    void resetHistory();
    wb::World m_world;
    std::shared_ptr<const wb::MeshScene> m_scene;
    std::unique_ptr<wb::Stroke> m_stroke;
    struct Entry {
        wb::Command command;
        uint64_t before, after;
    };
    std::vector<Entry> m_history;
    int m_cursor = 0, m_tool = 0, m_radius = 4, m_strength = 1, m_material = wb::Stone, m_exportProgress = 0;
    uint64_t m_state = 0, m_savedState = 0, m_nextState = 1, m_meshSerial = 0, m_epoch = 0, m_jobEpoch = 0;
    bool m_strokeChanged = false;
    QString m_path, m_status = "正在构建初始体素网格…";
    std::set<int> m_dirty;
    QTimer m_meshTimer;
    QFutureWatcher<std::shared_ptr<wb::MeshScene>> m_meshWatcher;
    QFutureWatcher<wb::ExportResult> m_exportWatcher;
    std::atomic_bool m_cancelExport = false;
};
