import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: win
    width: 1440; height: 900; minimumWidth: 1100; minimumHeight: 680
    visible: true
    title: (editor.modified ? "● " : "") + editor.projectName + " — Worldbench 0.2.1"
    color: "#171c24"
    font.family: "Microsoft YaHei UI"; font.pixelSize: 13
    palette.window: "#202630"; palette.windowText: "#e0e6ef"
    palette.base: "#151b23"; palette.alternateBase: "#293240"
    palette.text: "#e0e6ef"; palette.button: "#303b4b"; palette.buttonText: "#dce4ee"
    palette.placeholderText: "#7e8da1"
    palette.highlight: "#43a9bf"; palette.highlightedText: "#101b24"
    property string pendingAction: ""
    property bool allowClose: false
    function requestAction(action) {
        if (editor.exportBusy || editor.assetBusy) { errorBox.text = "请等待导出完成，或先取消导出。"; errorBox.open(); return }
        pendingAction = action
        if (editor.modified) unsaved.open(); else finishAction()
    }
    function finishAction() {
        const action = pendingAction; pendingAction = ""
        if (action === "new") editor.newWorld()
        else if (action === "open") openDialog.open()
        else if (action === "close") { allowClose = true; win.close() }
    }
    function saveCurrent() {
        if (editor.assetBusy) return
        if (editor.projectPath.length === 0) saveDialog.open()
        else if (editor.save()) finishAction()
    }
    onClosing: function(close) { if (!allowClose) { close.accepted = false; requestAction("close") } }
    menuBar: MenuBar {
        Menu {
            title: "文件"
            Action { text: "新建工程     Ctrl+N"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.requestAction("new") }
            Action { text: "打开工程…    Ctrl+O"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.requestAction("open") }
            Action { text: "保存         Ctrl+S"; enabled: !editor.assetBusy; onTriggered: win.saveCurrent() }
            Action { text: "另存为…"; enabled: !editor.assetBusy; onTriggered: saveDialog.open() }
            MenuSeparator {}
            Action { text: "导出 Minecraft 1.20.1…"; enabled: !editor.exportBusy && !editor.assetBusy; onTriggered: exportDialog.open() }
            MenuSeparator {}
            Action { text: "退出"; onTriggered: win.requestAction("close") }
        }
        Menu {
            title: "编辑"
            Action { text: "撤销    Ctrl+Z"; enabled: editor.canUndo && !editor.assetBusy; onTriggered: editor.undo() }
            Action { text: "重做    Ctrl+Y"; enabled: editor.canRedo && !editor.assetBusy; onTriggered: editor.redo() }
        }
        Menu {
            title: "工具"
            Action { text: "选择 / 导航"; onTriggered: editor.tool = -1 }
            MenuSeparator {}
            Action { text: "抬升 / 降低"; onTriggered: editor.tool = 0 }
            Action { text: "整平地表"; onTriggered: editor.tool = 1 }
            Action { text: "体积挖除"; onTriggered: editor.tool = 2 }
            Action { text: "体积填充"; onTriggered: editor.tool = 3 }
            Action { text: "材质涂绘"; onTriggered: editor.tool = 4 }
        }
        Menu {
            title: "视图"
            Action { text: "场景大纲 / 详细信息"; onTriggered: worldDock.dockWidget.show() }
            Action { text: "相机与鼠标设置"; onTriggered: { settingsContent.page = "camera"; settingsDialog.open() } }
            Action { text: "重置视角"; onTriggered: if(editor.cameraController) editor.cameraController.resetCamera() }
        }
        Menu {
            title: "资源"
            Action { text: "载入客户端 JAR / 基础 ZIP…"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: { resourceDialog.overlay = false; resourceDialog.open() } }
            Action { text: "叠加资源包 ZIP…"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: { resourceDialog.overlay = true; resourceDialog.open() } }
            Action { text: "管理资源来源"; onTriggered: { settingsContent.page = "resources"; settingsDialog.open() } }
        }
        Menu {
            title: "自动化"
            Action { text: "MCP / LLM 接入…"; onTriggered: mcpDialog.open() }
        }
        Menu {
            title: "帮助"
            Action { text: "视口操作说明"; onTriggered: { errorBox.text = "右键 + WASDQE 漫游\nAlt + 左键围绕视口中心环绕\n中键平移 / Alt + 右键推拉\n右键 + 滚轮调速 / F 聚焦指向位置\n雕刻时 Shift 反向 / [ ] 调整半径\n左侧选择工具并展开设置"; errorBox.open() } }
            Action { text: "关于 Worldbench"; onTriggered: { errorBox.text = "Worldbench 0.2.1 · 开发测试版\nC++20 / Qt 6.8.3 / QRhi D3D11\n本轮测试见仓库 TESTING.md。"; errorBox.open() } }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        ToolRail { Layout.preferredWidth: 56; Layout.fillHeight: true }
        Pane {
            objectName: "leftBrushSettings"
            visible: editor.tool >= 0
            Layout.preferredWidth: 280; Layout.fillHeight: true; padding: 0
            background: Rectangle { color: "#23262d"; border.color: "#343b46" }
            ScrollView {
                id: brushScroll; anchors.fill: parent; clip: true; contentWidth: availableWidth
                ToolPanel { width: brushScroll.availableWidth }
            }
        }
        KDDW.DockingArea {
            id: workspace; Layout.fillWidth: true; Layout.fillHeight: true
            uniqueName: "WorldbenchWorkspace"
            options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget
            persistentCentralItemComponent: ViewportPane {}
            KDDW.DockWidget { id: worldDock; uniqueName: "world-info"; source: "qrc:/WorldPanel.qml" }
            Component.onCompleted: {
                worldDock.dockWidget.title = "场景 / 详细信息"
                addDockWidget(worldDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(310, 0))
            }
        }
    }
    FileDialog {
        id: resourceDialog; property bool overlay: false
        title: overlay ? "叠加资源包" : "载入基础 Minecraft 资源"
        fileMode: FileDialog.OpenFile; nameFilters: ["Minecraft 资源 (*.jar *.zip)"]
        onAccepted: { editor.importResource(selectedFile, overlay); settingsContent.page = "resources"; settingsDialog.open() }
    }
    Dialog {
        id: settingsDialog; objectName: "editorSettingsDialog"
        title: settingsContent.page === "camera" ? "相机与鼠标设置" : "资源来源与导入诊断"
        modal: false; width: Math.min(680, win.width - 60); height: Math.min(650, win.height - 100)
        anchors.centerIn: parent; standardButtons: Dialog.Close
        contentItem: SettingsPanel { id: settingsContent; objectName: "settingsContent" }
    }
    Dialog {
        id: mcpDialog; title: "MCP / LLM 接入"; modal: true; width: 670; anchors.centerIn: parent
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            CheckBox { text: "允许本机 LLM 客户端操作当前编辑器"; checked: editor.mcpEnabled; onToggled: editor.mcpEnabled = checked }
            Label { text: "启用后复制下方 stdio 配置到支持 MCP 的客户端。配置仅对本次启用有效。\nLLM 可读取、填方块、雕刻、撤销、移动相机及截图；不提供任意代码执行。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#9caabb" }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 230; clip: true
                TextArea { text: editor.mcpConfiguration; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Consolas"; font.pixelSize: 12 }
            }
            Button { text: "复制 MCP 客户端配置"; enabled: editor.mcpEnabled; onClicked: editor.copyMcpConfiguration() }
            Label { text: editor.mcpActivity; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#79c2c8" }
        }
    }
    footer: Rectangle {
        height: 35; color: "#131922"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 14; spacing: 12
            Rectangle { width: 6; height: 6; radius: 3; color: editor.meshBusy ? "#e4b763" : "#5ac9b2" }
            Label { text: editor.status; color: "#a8b5c5"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
            Label { text: editor.exportBusy ? "烘焙 " + editor.exportProgress + "%" : editor.meshBusy ? "网格更新中" : "D3D11 · 体素"; color: "#73c7d7"; font.pixelSize: 11 }
            Button { text: "取消导出"; visible: editor.exportBusy; implicitHeight: 28; onClicked: editor.cancelExport() }
        }
    }
    Shortcut { sequence: "Ctrl+S"; onActivated: win.saveCurrent() }
    Shortcut { sequence: "Ctrl+O"; onActivated: win.requestAction("open") }
    Shortcut { sequence: "Ctrl+N"; onActivated: win.requestAction("new") }
    Shortcut { sequence: "Ctrl+Z"; enabled: win.activeFocusItem && win.activeFocusItem.objectName === "voxelViewport"; onActivated: editor.undo() }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]; enabled: win.activeFocusItem && win.activeFocusItem.objectName === "voxelViewport"; onActivated: editor.redo() }
    FileDialog {
        id: saveDialog; title: "保存 Worldbench 工程"; fileMode: FileDialog.SaveFile
        nameFilters: ["Worldbench 工程 (*.wbench)"]; defaultSuffix: "wbench"
        onAccepted: { if (editor.save(selectedFile)) win.finishAction() }
        onRejected: win.pendingAction = ""
    }
    FileDialog { id: openDialog; title: "打开 Worldbench 工程"; fileMode: FileDialog.OpenFile; nameFilters: ["Worldbench 工程 (*.wbench)"]; onAccepted: editor.open(selectedFile) }
    MessageDialog {
        id: unsaved; title: "保存当前改动？"; text: "当前工程有未保存的改动。"
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Save) win.saveCurrent()
            else if (button === MessageDialog.Discard) win.finishAction()
            else win.pendingAction = ""
        }
    }
    MessageDialog { id: errorBox; title: "Worldbench"; buttons: MessageDialog.Ok }
    FolderDialog { id: exportFolder; title: "选择存放新 Minecraft 存档的父目录"; onAccepted: exportParent.text = selectedFolder.toString() }
    Dialog {
        id: exportDialog; title: "导出 Minecraft Java 1.20.1"; modal: true
        anchors.centerIn: parent; width: 520
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: 13
            Label { text: "将当前编辑结果烘焙为独立游戏存档。"; color: "#e0e6ef" }
            Label { text: "父目录（可选择 .minecraft/saves）"; color: "#96a8bc" }
            RowLayout {
                TextField { id: exportParent; Layout.fillWidth: true; readOnly: true; placeholderText: "选择已有目录…" }
                Button { text: "浏览"; onClicked: exportFolder.open() }
            }
            Label { text: "新存档名称"; color: "#96a8bc" }
            TextField { id: exportName; text: "Worldbench-001"; Layout.fillWidth: true; selectByMouse: true; maximumLength: 64 }
            Label { text: "使用字母、数字、短横线或下划线。已存在的目录不会被覆盖。\nP0：地形范围外为虚空；光照由游戏重新计算。\n不含实体/方块实体、其他版本或自定义高度。动态方块需 1.20.1 JAR。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12; color: "#9aaabe" }
        }
        onAccepted: editor.exportWorld(exportParent.text, exportName.text)
    }
    Connections {
        target: editor
        function onFailure(message) { errorBox.text = message; errorBox.open() }
        function onExported(path) { errorBox.text = "导出完成：\n" + path + "\n\n请在 Minecraft Java 1.20.1 中测试。"; errorBox.open() }
    }
}
