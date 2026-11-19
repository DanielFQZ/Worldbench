#pragma once
#include "core/Mesh.h"
#include "core/MinecraftExport.h"
#include "core/World.h"
#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <set>
class McpServer;

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
    Q_PROPERTY(bool assetBusy READ assetBusy NOTIFY changed)
    Q_PROPERTY(int assetProgress READ assetProgress NOTIFY assetProgressChanged)
    Q_PROPERTY(QString assetStage READ assetStage NOTIFY assetProgressChanged)
    Q_PROPERTY(QString assetLog READ assetLog NOTIFY assetProgressChanged)
    Q_PROPERTY(QStringList defaultResourceSources READ defaultResourceSources NOTIFY changed)
    Q_PROPERTY(QVariantMap brushOptions READ brushOptions NOTIFY changed)
    Q_PROPERTY(QString meshUpdateSummary READ meshUpdateSummary NOTIFY changed)
    Q_PROPERTY(QString resourceSummary READ resourceSummary NOTIFY changed)
    Q_PROPERTY(QString resourceDiagnostics READ resourceDiagnostics NOTIFY changed)
    Q_PROPERTY(QVariantList sceneNodes READ sceneNodes NOTIFY changed)
    Q_PROPERTY(QVariantMap layerDetails READ layerDetails NOTIFY changed)
    Q_PROPERTY(QString materialLabel READ materialLabel NOTIFY changed)
    Q_PROPERTY(QVariantList materialProperties READ materialProperties NOTIFY changed)
    Q_PROPERTY(QStringList resourceSources READ resourceSources NOTIFY changed)
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY changed)
    Q_PROPERTY(QString mcpConfiguration READ mcpConfiguration NOTIFY changed)
    Q_PROPERTY(QString mcpActivity READ mcpActivity NOTIFY changed)
    Q_PROPERTY(QString selectedNode READ selectedNode WRITE setSelectedNode NOTIFY changed)
    Q_PROPERTY(QObject *cameraController READ cameraController NOTIFY changed)
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
    bool assetBusy() const { return m_assetsLoading; }
    int assetProgress() const { return m_assetProgress; }
    QString assetStage() const { return m_assetStage; }
    QString assetLog() const { return m_assetLog.join('\n'); }
    QStringList defaultResourceSources() const;
    QVariantMap brushOptions() const;
    int brushShape() const { return m_brushOptions.shape; }
    QString meshUpdateSummary() const;
    Q_INVOKABLE void setBrushOption(const QString &name, const QVariant &value);
    Q_INVOKABLE void initializeResources();
    Q_INVOKABLE QUrl dialogFolder(const QString &key) const;
    Q_INVOKABLE void rememberDialogFolder(const QString &key, const QUrl &url, bool directory = false);
    Q_INVOKABLE void clearDefaultResources();
    QString resourceSummary() const;
    QString resourceDiagnostics() const;
    QVariantList sceneNodes() const;
    QVariantMap layerDetails() const;
    QString materialLabel() const;
    QVariantList materialProperties() const;
    QStringList resourceSources() const { return m_world.resourcePaths; }
    bool mcpEnabled() const;
    void setMcpEnabled(bool enabled);
    QString mcpConfiguration() const;
    QString mcpActivity() const;
    QString selectedNode() const { return m_selectedNode; }
    void setSelectedNode(const QString &node) {
        if (node != "terrain-base-1" || node == m_selectedNode)
            return;
        m_selectedNode = node;
        emit changed();
    }
    QObject *cameraController() const { return m_cameraController; }
    void setCameraController(QObject *controller) {
        m_cameraController = controller;
        emit changed();
    }
    uint64_t revision() const { return m_revision; }
    bool commandBusy() const { return bool(m_stroke) || assetBusy(); }
    QJsonObject worldInfo() const;
    QJsonObject executeTool(const QString &name, const QJsonObject &args);
    Q_INVOKABLE QVariantList blockCatalog(const QString &filter = QString()) const;
    Q_INVOKABLE void selectBlock(const QString &name);
    Q_INVOKABLE void setBlockProperty(const QString &key, const QString &value);
    Q_INVOKABLE void importResource(const QUrl &url, bool overlay);
    Q_INVOKABLE void removeResource(int index);
    Q_INVOKABLE void copyMcpConfiguration();
    bool modified() const {
        return m_strokeChanged || m_state != m_savedState || m_resourceRevision != m_savedResourceRevision;
    }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < int(m_history.size()); }
    bool meshBusy() const { return m_meshActive || !m_dirty.empty(); }
    bool exportBusy() const { return m_exportActive; }
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
    Q_INVOKABLE bool createWorld(int size, int seed, int baseHeight, int variation, int seaLevel, bool flat);
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
    void catalogChanged();
    void assetProgressChanged();
    void resourceLoadStarted();
    void resourceSetupRequested();

  private:
    void dirtyAll();
    void dirtyCommand(const wb::Command &command);
    void startMesh();
    void resetHistory();
    void pushCommand(wb::Command command);
    void loadResources(const QStringList &paths, bool restoring = false, bool rememberDefault = false);
    void reportAssetProgress(int progress, const QString &message);
    static QString resourceFingerprint(const QStringList &paths);
    wb::BrushOptions m_brushOptions;
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
    struct AssetResult {
        std::shared_ptr<const wb::AssetLibrary> library;
        std::vector<std::shared_ptr<const wb::BlockModel>> models;
        QString error;
    };
    QFutureWatcher<AssetResult> m_assetWatcher;
    std::unique_ptr<McpServer> m_mcp;
    uint64_t m_revision = 1, m_resourceRevision = 0, m_savedResourceRevision = 0;
    QString m_selectedNode = "terrain-base-1";
    QPointer<QObject> m_cameraController;
    bool m_restoringAssets = false;
    bool m_rememberDefault = false;
    int m_assetProgress = 0;
    QString m_assetStage;
    QStringList m_assetLog;
    std::shared_ptr<const wb::AssetLibrary> m_defaultAssets;
    QString m_defaultFingerprint, m_loadingFingerprint;
    bool m_assetsLoading = false, m_meshActive = false, m_exportActive = false;
    std::atomic_bool m_cancelExport = false;
};
