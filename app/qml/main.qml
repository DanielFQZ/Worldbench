import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: win
    width: 1440; height: 900; minimumWidth: 1024; minimumHeight: 680
    visible: true
    title: (editor.modified ? "● " : "") + editor.projectName + " — Worldbench 0.1.0"
    color: "#171c24"
    font.family: "Microsoft YaHei UI"; font.pixelSize: 13
    palette.window: "#202630"; palette.windowText: "#e0e6ef"
    palette.base: "#151b23"; palette.alternateBase: "#293240"
    palette.text: "#e0e6ef"; palette.button: "#303b4b"; palette.buttonText: "#dce4ee"
    palette.highlight: "#43a9bf"; palette.highlightedText: "#101b24"
    property string pendingAction: ""
    property bool allowClose: false
    function requestAction(action) {
        if (editor.exportBusy) { errorBox.text = "请等待导出完成，或先取消导出。"; errorBox.open(); return }
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
        if (editor.projectPath.length === 0) saveDialog.open()
        else if (editor.save()) finishAction()
    }
    onClosing: function(close) { if (!allowClose) { close.accepted = false; requestAction("close") } }
    header: Rectangle {
        height: 66; color: "#171e28"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 18; spacing: 9
            ColumnLayout {
                spacing: 0; Layout.rightMargin: 22
                Label { text: "WORLDBENCH"; font.pixelSize: 19; font.bold: true; font.letterSpacing: 2; color: "#e7f2f5" }
                Label { text: "0.1.0  /  P0 体素编辑测试版"; font.pixelSize: 10; color: "#7992a8" }
            }
            Button { text: "新建"; onClicked: win.requestAction("new"); enabled: !editor.exportBusy }
            Button { text: "打开"; onClicked: win.requestAction("open"); enabled: !editor.exportBusy }
            Button { text: "保存"; onClicked: win.saveCurrent() }
            Rectangle { width: 1; height: 24; color: "#394354"; Layout.leftMargin: 7; Layout.rightMargin: 7 }
            Button { text: "撤销"; enabled: editor.canUndo; onClicked: editor.undo() }
            Button { text: "重做"; enabled: editor.canRedo; onClicked: editor.redo() }
            Item { Layout.fillWidth: true }
            Button { text: "工具面板"; onClicked: toolDock.dockWidget.show() }
            Button { text: "世界面板"; onClicked: worldDock.dockWidget.show() }
            Button { text: "导出 Minecraft"; highlighted: true; enabled: !editor.exportBusy; onClicked: exportDialog.open() }
        }
    }
    KDDW.DockingArea {
        id: workspace; anchors.fill: parent
        uniqueName: "WorldbenchWorkspace"
        options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget
        persistentCentralItemComponent: ViewportPane {}
        KDDW.DockWidget { id: toolDock; uniqueName: "sculpt-tools"; source: "qrc:/ToolPanel.qml" }
        KDDW.DockWidget { id: worldDock; uniqueName: "world-info"; source: "qrc:/WorldPanel.qml" }
        Component.onCompleted: {
            toolDock.dockWidget.title = "雕刻工具"
            worldDock.dockWidget.title = "世界与操作"
            addDockWidget(toolDock, KDDW.KDDockWidgets.Location_OnLeft, null, Qt.size(255, 0))
            addDockWidget(worldDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(260, 0))
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
            Label { text: "使用字母、数字、短横线或下划线。已存在的目录不会被覆盖。\nP0：地形范围外为虚空；光照由游戏重新计算。\n不含实体、复杂方块、其他版本或自定义高度。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12; color: "#9aaabe" }
        }
        onAccepted: editor.exportWorld(exportParent.text, exportName.text)
    }
    Connections {
        target: editor
        function onFailure(message) { errorBox.text = message; errorBox.open() }
        function onExported(path) { errorBox.text = "导出完成：\n" + path + "\n\n请在 Minecraft Java 1.20.1 中测试。"; errorBox.open() }
    }
}
