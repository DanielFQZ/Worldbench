import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: win
    width: 1440; height: 900; minimumWidth: 1100; minimumHeight: 680
    visible: true
    readonly property string productLabel: ("Worldbench " + Qt.application.version).trim()
    title: (editor.modified ? "● " : "") + editor.projectName + " — " + productLabel
    flags: Qt.platform.os === "windows" ? Qt.FramelessWindowHint | Qt.Window : Qt.Window
    color: Theme.window
    font.family: "Microsoft YaHei UI"; font.pixelSize: 13
    palette.window: Theme.panel; palette.windowText: Theme.text
    palette.base: Theme.field; palette.alternateBase: Theme.raised
    palette.text: Theme.text; palette.button: Theme.raised; palette.buttonText: Theme.text
    palette.placeholderText: Theme.muted
    palette.highlight: Theme.selectionHover; palette.highlightedText: Theme.text
    palette.light: Theme.border; palette.midlight: Theme.raised; palette.mid: Theme.border; palette.dark: Theme.field; palette.shadow: Theme.divider
    ListModel { id: openTabs }
    property string activeId: ""
    readonly property bool inScene: activeId === ""
    readonly property int openAssetCount: openTabs.count
    property var catalog: editor.libraryAssets
    readonly property var activeAsset: activeId === "@road" ? {id:"@road",kind:"road",name:"道路网络"} :
                                               catalog.find(a => a.id === activeId) || {}
    property bool restoreSceneDock: true
    function activate(id) {
        if (activeId === id) return
        if (inScene && id) restoreSceneDock = worldDock.isOpen
        activeId = id
        if (activeAsset.kind === "terrain") editor.selectedNode = id
        if (id && worldDock.isOpen) worldDock.close()
        else if (!id && restoreSceneDock) worldDock.open()
    }
    function openAsset(id) {
        const asset = id === "@road" ? {id:id,name:"道路网络"} : editor.libraryAssets.find(a => a.id === id)
        if (!asset) return
        for (let i=0; i<openTabs.count; ++i)
            if (openTabs.get(i).assetId === id) { activate(id); tabStrip.positionViewAtIndex(i,ListView.Contain); return }
        openTabs.append({assetId:id,caption:asset.name})
        activate(id)
        tabStrip.positionViewAtEnd()
    }
    function closeTab(id) {
        for (let i=0; i<openTabs.count; ++i) {
            if (openTabs.get(i).assetId !== id) continue
            if (activeId === id) activate("")
            openTabs.remove(i)
            break
        }
    }
    function syncTabs() {
        for (let i=openTabs.count-1; i>=0; --i) {
            const tab=openTabs.get(i)
            if (tab.assetId === "@road") continue
            const asset=catalog.find(a => a.id === tab.assetId)
            if (!asset) closeTab(tab.assetId)
            else if (asset.name !== tab.caption) openTabs.setProperty(i,"caption",asset.name)
        }
    }
    onCatalogChanged: if (openTabs.count) syncTabs()
    Connections { target:editor; function onWorldReset(){win.activate("");openTabs.clear()} }
    function newGraph(material) {
        if (editor.createPcgGraph("",material)) openAsset(editor.pcgGraphs[editor.pcgGraphs.length-1].id)
    }
    function importAsset() {
        structureFile.currentFolder=editor.dialogFolder("structureImport")
        structureFile.open()
    }
    FileDialog {
        id:structureFile; title:"导入资产"
        nameFilters:["结构或 PCG (*.schem *.schematic *.nbt *.json *.wbasset *.wbpcg)"]
        onRejected:editor.rememberDialogFolder("structureImport",currentFolder,true)
        onAccepted:{
            editor.rememberDialogFolder("structureImport",selectedFile)
            const before=editor.libraryAssets.map(a=>a.id)
            const graph=String(selectedFile).toLowerCase().endsWith(".wbpcg")
            if (graph ? editor.creationFile("load_graph","",selectedFile) : editor.importStructureAsset(selectedFile)) {
                const imported=editor.libraryAssets.find(a=>!before.includes(a.id) && (!graph || a.collection==="graphs"))
                if(imported) win.openAsset(imported.id)
            }
        }
    }
    property string pendingAction: ""
    property int shortcutRevision: 0
    function shortcut(action) { shortcutRevision; return editor.shortcut(action) }
    Connections { target: editor; function onShortcutsChanged() { win.shortcutRevision++ } }
    function editingText() {
        const item = activeFocusItem
        return item && (typeof item.selectAll === "function" || typeof item.inputMethodComposing === "boolean")
    }
    Shortcut {sequence:win.shortcut("clearSelection");enabled:win.inScene&&!win.editingText()&&!editor.commandBusy;onActivated:if(!editor.commandBusy)editor.clearSelection()}
    Shortcut {sequence:win.shortcut("deleteTerrain");enabled:win.inScene&&!win.editingText()&&!editor.commandBusy&&!editor.exportBusy&&editor.terrainDetails.valid;onActivated:if(!editor.commandBusy)editor.removeTerrain()}
    property url pendingProject: ""
    function requestProjectDrop(urls) {
        if (!editor.acceptsProjectDrop(urls)) return false
        if (editor.assetBusy || editor.exportBusy) return false
        pendingProject = urls[0]
        requestAction("drop")
        return true
    }
    property bool allowClose: false
    property bool autoCloseResources:false
    Item {
        // Cover the complete window, including its menu/title area. Content
        // coordinates otherwise leave the top border below the menu bar.
        parent: win.contentItem
        x: -win.contentItem.x; y: -win.contentItem.y
        width: win.width; height: win.height; z: 1000
        visible: Qt.platform.os === "windows" && win.visibility !== Window.Maximized
        Repeater {
            model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                    Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge,
                    Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
            MouseArea {
                required property int modelData
                readonly property bool corner: (modelData & (Qt.LeftEdge | Qt.RightEdge)) && (modelData & (Qt.TopEdge | Qt.BottomEdge))
                readonly property int grip: corner ? 9 : 5
                width: (modelData & (Qt.LeftEdge | Qt.RightEdge)) ? grip : parent.width - 18
                height: (modelData & (Qt.TopEdge | Qt.BottomEdge)) ? grip : parent.height - 18
                x: (modelData & Qt.RightEdge) ? parent.width - width : (modelData & Qt.LeftEdge) ? 0 : 9
                y: (modelData & Qt.BottomEdge) ? parent.height - height : (modelData & Qt.TopEdge) ? 0 : 9
                cursorShape: corner ? ((modelData === (Qt.TopEdge | Qt.LeftEdge) || modelData === (Qt.BottomEdge | Qt.RightEdge)) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : (modelData & (Qt.LeftEdge | Qt.RightEdge)) ? Qt.SizeHorCursor : Qt.SizeVerCursor
                onPressed: win.startSystemResize(modelData)
            }
        }
    }
    menuBar: Column {
      width: win.width
      Rectangle {
        id: windowChrome
        objectName: "windowChrome"
        width: parent.width
        height: visible ? 32 : 0
        visible: Qt.platform.os === "windows"
        color: Theme.window
        border.color: Theme.border
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 6
            spacing: 9
            Rectangle {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 3
                color: "#2a7288"
                border.color: Theme.accent
                Text { anchors.centerIn: parent; text: "W"; color: Theme.text; font.bold: true; font.pixelSize: 15 }
            }
            Text {
                text: win.title
                color: Theme.text
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            WindowButton { id: minimizeButton; objectName: "windowMinimize"; Layout.minimumWidth: 42; Layout.maximumWidth: 42; Layout.preferredHeight: 30; symbol: "minimize"; ToolTip.text: "最小化"; ToolTip.visible: hovered; onClicked: win.showMinimized() }
            WindowButton { objectName: "windowMaximize"; Layout.minimumWidth: 42; Layout.maximumWidth: 42; Layout.preferredHeight: 30; symbol: win.visibility === Window.Maximized ? "restore" : "maximize"; ToolTip.text: "最大化 / 还原"; ToolTip.visible: hovered; onClicked: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized() }
            WindowButton { objectName: "windowClose"; Layout.minimumWidth: 42; Layout.maximumWidth: 42; Layout.preferredHeight: 30; symbol: "close"; ToolTip.text: "关闭"; ToolTip.visible: hovered; onClicked: win.requestAction("close") }
        }
        MouseArea {
            objectName: "windowTitleDrag"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: minimizeButton.x + 12
            onPressed: win.startSystemMove()
            onDoubleClicked: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized()
        }
      }
      MenuBar {
        objectName: "mainMenuBar"
        width: parent.width
        background:Rectangle{color:Theme.panel}
        delegate:MenuBarItem {
            id:barItem
            implicitHeight:28; leftPadding:12; rightPadding:12
            contentItem:Text{text:barItem.text;font:barItem.font;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;color:barItem.highlighted?Theme.text:Theme.text}
            background:Rectangle{color:barItem.highlighted?Theme.selection:"transparent"}
        }
        WorkbenchMenu {
            objectName: "fileMenu"
            title: "文件"
            Action { text: "新建工程     Ctrl+N"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.requestAction("new") }
            Action { text: "打开工程…    Ctrl+O"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.requestAction("open") }
            Action { text: "保存         Ctrl+S"; enabled: !editor.assetBusy; onTriggered: win.saveCurrent() }
            Action { text: "另存为…"; enabled: !editor.assetBusy; onTriggered: win.chooseSave() }
            MenuSeparator {}
            Action { text: "导出 Minecraft 1.20.1…"; enabled: !editor.exportBusy && !editor.assetBusy; onTriggered: exportDialog.open() }
            MenuSeparator {}
            Action { text: "退出"; onTriggered: win.requestAction("close") }
        }
        WorkbenchMenu {
            title: "编辑"
            Action { text: "撤销    Ctrl+Z"; enabled: editor.canUndo && !editor.assetBusy; onTriggered: editor.undo() }
            Action { text: "重做    Ctrl+Y"; enabled: editor.canRedo && !editor.assetBusy; onTriggered: editor.redo() }
            Action {text:"清除选区    Ctrl+D";enabled:editor.selection.valid&&!editor.commandBusy;onTriggered:editor.clearSelection()}
            Action {text:"删除地貌实例    Delete";enabled:win.inScene&&editor.terrainDetails.valid&&!editor.commandBusy&&!editor.exportBusy;onTriggered:editor.removeTerrain()}
            MenuSeparator {}
            Action { text: "偏好设置…"; onTriggered: { settingsContent.page = "camera"; settingsDialog.open() } }
        }
        WorkbenchMenu {
            title: "工具"
            Action { text: "选择 / 导航"; onTriggered: editor.tool = -1 }
            MenuSeparator {}
            Action { text: "抬升 / 降低"; onTriggered: editor.tool = 0 }
            Action { text: "整平地表"; onTriggered: editor.tool = 1 }
            Action { text: "体积挖除"; onTriggered: editor.tool = 2 }
            Action { text: "体积填充"; onTriggered: editor.tool = 3 }
            Action { text: "材质涂绘"; onTriggered: editor.tool = 4 }
            Action { text: "平滑地形"; onTriggered: editor.tool = 5 }
            MenuSeparator {}
            Action { text: "导入高度图地貌 PNG…";enabled:!editor.assetBusy&&!editor.exportBusy;onTriggered:{heightmapDialog.currentFolder=editor.dialogFolder("heightmap");heightmapDialog.open()} }
            Action {text:"地表高度主题…";enabled:!editor.commandBusy&&!editor.exportBusy;onTriggered:surfaceThemeDialog.open()}
        }
        WorkbenchMenu {
            title: "创作"
            Action { text:"新建 PCG 图"; enabled:!editor.commandBusy; onTriggered:win.newGraph(false) }
            Action { text:"新建地表材质"; enabled:!editor.commandBusy; onTriggered:win.newGraph(true) }
            Action { text:"导入资产…"; enabled:!editor.commandBusy; onTriggered:win.importAsset() }
            Action { text:"道路网络"; onTriggered:win.openAsset("@road") }
        }
        WorkbenchMenu {
            title: "视图"
            Action { text: "场景大纲 / 详细信息"; onTriggered: { win.activate(""); worldDock.open(); win.restoreSceneDock=true } }
            Action { text: "跳转至坐标…"; onTriggered: jumpDialog.open() }
            Action { text: "矩形选区"; onTriggered: editor.tool=6 }
            Action { text: "重置视角"; onTriggered: if(editor.cameraController) editor.cameraController.resetCamera() }
        }
        WorkbenchMenu {
            title: "资源"
            Action { text: "载入客户端 JAR / 基础 ZIP…"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.chooseResources(false) }
            Action { text: "叠加资源包 ZIP…"; enabled: !editor.assetBusy && !editor.exportBusy; onTriggered: win.chooseResources(true) }
            Action { text: "管理资源来源"; onTriggered: { win.autoCloseResources=false; settingsContent.page = "resources"; settingsDialog.open() } }
        }
        WorkbenchMenu {
            title: "自动化"
            Action { text: "MCP / LLM 接入…"; onTriggered: mcpDialog.open() }
        }
        WorkbenchMenu {
            title: "帮助"
            Action { text: "视口操作说明"; onTriggered: { errorBox.text = "右键 + WASDQE 漫游\nAlt + 左键围绕视口中心环绕\n中键平移 / Alt + 右键推拉\n右键 + 滚轮调速 / F 聚焦选中实例或指向位置\n雕刻时 Shift 反向 / [ ] 调整半径\n左侧选择工具并展开设置\n地图图标显示全图，导航工具双击地表进入近景\n抬升/平整/平滑可从远处直接落笔\n太阳图标开关光照，参数在环境组件详情"; errorBox.open() } }
            Action { text: "关于 Worldbench"; onTriggered: { errorBox.text = win.productLabel + " · 开发测试版\nC++20 / Qt 6.8.3 / QRhi D3D11\n本轮测试见仓库 TESTING.md。"; errorBox.open() } }
            Action { text: "检查更新…"; enabled: !updater.downloading; onTriggered: win.checkUpdates() }
        }
    }
      Rectangle {
        width:parent.width; height:34; color:Theme.window
        RowLayout {
            anchors.fill:parent; spacing:0
            WorkbenchButton {
                objectName:"sceneTab";text:"场景";flat:true;background:Rectangle{color:Theme.window;Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:2;color:win.inScene?Theme.accent:Theme.divider}}
                checked:win.inScene;Layout.preferredWidth:84;Layout.fillHeight:true
                onClicked:win.activate("")
            }
            ListView {
                id:tabStrip;objectName:"workspaceTabs";Layout.fillWidth:true;Layout.fillHeight:true
                orientation:ListView.Horizontal;clip:true;model:openTabs
                delegate:Rectangle {
                    id:tab;required property string assetId;required property string caption
                    width:Math.min(235,Math.max(130,label.implicitWidth+52));height:tabStrip.height
                    color:label.hovered || closeButton.hovered ? Theme.hover : win.activeId===assetId ? Theme.raised : Theme.window
                    RowLayout {
                        anchors.fill:parent;spacing:0
                        WorkbenchButton {
                            id:label;objectName:"openTab-"+tab.assetId;text:tab.caption;background:Item{}
                            flat:true;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.fillHeight:true
                            onClicked:win.activate(tab.assetId)
                        }
                        WorkbenchButton {
                            id:closeButton;objectName:"closeTab-"+tab.assetId;text:"×";flat:true;background:Item{}
                            Layout.preferredWidth:26;Layout.fillHeight:true
                            Accessible.name:"关闭 "+tab.caption
                            onClicked:win.closeTab(tab.assetId)
                        }
                    }
                    Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:2;color:win.activeId===tab.assetId?Theme.accent:Theme.divider}
                }
            }
        }
        Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:1;color:Theme.divider}
      }
    }
    function requestAction(action) {
        if (editor.exportBusy || editor.assetBusy) { errorBox.text = editor.regionBusy ? "请等待区域操作完成，或点击底栏的取消区域操作。" : "请等待当前资源处理或导出完成，导出可从底栏取消。"; errorBox.open(); return }
        pendingAction = action
        if (editor.modified) unsaved.open(); else finishAction()
    }
    function finishAction() {
        const action = pendingAction; pendingAction = ""
        if (action === "new") newWorldDialog.open()
        else if (action === "open") { openDialog.currentFolder=editor.dialogFolder("openProject"); openDialog.open() }
        else if (action === "drop") { const url = pendingProject; pendingProject=""; editor.rememberDialogFolder("openProject",url); editor.open(url) }
        else if (action === "close") { allowClose = true; win.close() }
    }
    function chooseResources(overlay) {
        resourceDialog.overlay=overlay
        resourceDialog.currentFolder=editor.dialogFolder(overlay?"overlayResource":"baseResource")
        resourceDialog.open()
    }
    function chooseSave() { saveDialog.currentFolder=editor.dialogFolder("saveProject"); saveDialog.open() }
    function saveCurrent() {
        if (editor.assetBusy) return
        if (editor.projectPath.length === 0) chooseSave()
        else if (editor.save()) finishAction()
    }
    onClosing: function(close) { if (!allowClose) { close.accepted = false; requestAction("close") } }
    SurfaceThemeDialog{id:surfaceThemeDialog}
    RowLayout {
        anchors.fill: parent; spacing: 0
        ToolRail { visible:win.inScene; Layout.preferredWidth: 46; Layout.fillHeight: true }
        Pane {
            objectName: "leftBrushSettings"
            visible: win.inScene && editor.tool >= 0
            Layout.preferredWidth: 256; Layout.fillHeight: true; padding: 0
            background: Rectangle { color: Theme.panel; border.color: Theme.border }
            ScrollView {
                id: brushScroll; anchors.fill: parent; clip: true; contentWidth: availableWidth
                Column{width:brushScroll.availableWidth;ToolPanel{width:parent.width;visible:editor.tool<=5} SelectionPanel{width:parent.width;visible:editor.tool===6}}
            }
        }
        KDDW.DockingArea {
            id: workspace; Layout.fillWidth: true; Layout.fillHeight: true
            uniqueName: "WorldbenchWorkspace"
            options: KDDW.KDDockWidgets.MainWindowOption_HasCentralWidget
            persistentCentralItemComponent: Item {
                anchors.fill:parent
                ViewportPane {
                    id:sceneViewport;visible:win.inScene
                    onAssetRequested:id=>win.openAsset(id)
                    onImportRequested:win.importAsset()
                    onGraphRequested:material=>win.newGraph(material)
                }
                CreationPanel {
                    anchors.fill:parent;visible:!win.inScene;asset:win.activeAsset
                    onPlaceRequested:win.activate("")
                    onAssetRequested:id=>win.openAsset(id)
                }
                Connections {target:win;function onInSceneChanged(){if(win.inScene)Qt.callLater(function(){sceneViewport.focusViewport()})}}
            }
            KDDW.DockWidget { id: worldDock; uniqueName: "world-info"; source: "qrc:/WorldPanel.qml" }
            Component.onCompleted: {
                worldDock.dockWidget.title = "场景"
                addDockWidget(worldDock, KDDW.KDDockWidgets.Location_OnRight, null, Qt.size(310, 0))
            }
        }
    }
    DropArea {
        objectName:"projectDropArea";anchors.fill:parent;z:50
        onEntered:function(drag){drag.accepted=!editor.assetBusy&&!editor.exportBusy&&editor.acceptsProjectDrop(drag.urls)}
        onDropped:function(drop){if(win.requestProjectDrop(drop.urls))drop.acceptProposedAction()}
        Rectangle{anchors.fill:parent;visible:parent.containsDrag;color:"#302080a0";border.color:Theme.accent;border.width:3
            Label{anchors.centerIn:parent;text:"松开以打开 Worldbench 工程";color:Theme.text;font.pixelSize:22}
        }
    }
    Timer{id:resourceCloseTimer;interval:350;onTriggered:{if(!win.autoCloseResources)return;if(editor.assetBusy||editor.meshBusy){restart();return}settingsDialog.close();win.autoCloseResources=false}}
    FileDialog{id:heightmapDialog;title:"导入地貌实例：8/16 位灰度 PNG";fileMode:FileDialog.OpenFile;nameFilters:["灰度 PNG (*.png)"];onAccepted:{editor.rememberDialogFolder("heightmap",selectedFile);editor.importHeightmap(selectedFile)}onRejected:editor.rememberDialogFolder("heightmap",currentFolder,true)}
    Dialog {
        id:jumpDialog;title:"跳转至坐标";modal:true;anchors.centerIn:parent;width:440;standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:ColumnLayout{
            Label{text:"移动到地图内的指定位置，自动加载附近区块。";wrapMode:Text.WordWrap;Layout.fillWidth:true}
            RowLayout{Label{text:"X"} WorkbenchSpinBox{id:jumpX;from:editor.layerDetails.originX;to:from+editor.layerDetails.width-1;editable:true;Layout.fillWidth:true}}
            RowLayout{Label{text:"Z"} WorkbenchSpinBox{id:jumpZ;from:editor.layerDetails.originZ;to:from+editor.layerDetails.depth-1;editable:true;Layout.fillWidth:true}}
        }
        onAccepted:if(editor.cameraController)editor.cameraController.jumpTo(jumpX.value,jumpZ.value)
    }
    FileDialog {
        id: resourceDialog; property bool overlay: false
        title: overlay ? "叠加资源包" : "载入基础 Minecraft 资源"
        fileMode: FileDialog.OpenFile; nameFilters: ["Minecraft 资源 (*.jar *.zip)"]
        onAccepted: { editor.rememberDialogFolder(overlay?"overlayResource":"baseResource",selectedFile); editor.importResource(selectedFile, overlay) }
        onRejected: editor.rememberDialogFolder(overlay?"overlayResource":"baseResource",currentFolder,true)
    }
    NewWorldDialog { id:newWorldDialog;anchors.centerIn:parent }
    Dialog {
        id:resourceSetup;title:"设置默认 Minecraft 资源";modal:true;width:520;anchors.centerIn:parent
        standardButtons:Dialog.Close
        contentItem:ColumnLayout {
            Label{text:"选择一次客户端 JAR，此后启动或新建地图会自动加载。你可以随时从资源菜单更换。";Layout.fillWidth:true;wrapMode:Text.WordWrap}
            WorkbenchButton {text:"选择客户端 JAR / 基础 ZIP…";onClicked:{resourceSetup.close();win.chooseResources(false)}}
            Label{text:"也可以稍后设置，先用内置材质试用。";color:Theme.secondary}
        }
    }
    function checkUpdates() {
        updateDialog.open()
        updater.checkForUpdates(true)
    }
    Connections {
        target: updater
        function onCheckFinished(available) { if (available) updateDialog.open() }
    }
    Timer { id: updateCheckTimer; interval: 1800; running: Qt.application.applicationName === "Worldbench"; repeat: false; onTriggered: updater.checkForUpdates(false) }
    Dialog {
        id: updateDialog; objectName: "updateDialog"; title: "Worldbench 更新"; modal: true
        width: Math.min(680, win.width - 80); height: Math.min(540, win.height - 120)
        anchors.centerIn: parent; standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                objectName: "updateHeading"
                text: updater.checking ? "正在检查更新…" : updater.updateAvailable ? "发现新版本 " + updater.latestVersion : "Worldbench 更新检查"
                color: Theme.text; font.bold: true; font.pixelSize: 17; Layout.fillWidth: true
            }
            Label { text: updater.status; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: "当前版本：" + updater.currentVersion; color: Theme.muted }
            BusyIndicator { running: updater.checking; visible: running; Layout.alignment: Qt.AlignHCenter }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                TextArea {
                    text: updater.updateAvailable ? updater.releaseNotes : ""
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    color: Theme.text; background: Rectangle { color: Theme.field; border.color: Theme.border; radius: 3 }
                }
            }
            ProgressBar { visible: updater.downloading; Layout.fillWidth: true; from: 0; to: 100; value: updater.progress }
            Label { visible: updater.downloading; text: updater.progress + "%"; color: Theme.muted; Layout.alignment: Qt.AlignRight }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Item { Layout.fillWidth: true }
                WorkbenchButton { text: "打开发布页"; enabled: updater.releaseUrl.length > 0; onClicked: updater.openReleasePage() }
                WorkbenchButton { text: updater.downloading ? "下载中…" : "立即更新"; enabled: updater.updateAvailable && !updater.downloading; onClicked: updater.installUpdate() }
                WorkbenchButton { text: updater.updateAvailable ? "稍后" : "关闭"; enabled: !updater.downloading; onClicked: updateDialog.close() }
            }
        }
    }
    Dialog {
        id: settingsDialog; objectName: "editorSettingsDialog"
        title: "偏好设置"
        modal: false; width: Math.min(900, win.width - 60); height: Math.min(680, win.height - 100)
        anchors.centerIn: parent; standardButtons: Dialog.Close
        contentItem: SettingsPanel { id: settingsContent; objectName: "settingsContent" }
    }
    Dialog {
        id: mcpDialog; title: "MCP / LLM 接入"; modal: true; width: 670; anchors.centerIn: parent
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            WorkbenchCheckBox { text: "允许本机 LLM 客户端操作当前编辑器"; checked: editor.mcpEnabled; onToggled: editor.mcpEnabled = checked }
            Label { text: "启用后复制下方 stdio 配置到支持 MCP 的客户端。配置仅对本次启用有效。\nLLM 可读取、填方块、雕刻、撤销、移动相机及截图；不提供任意代码执行。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: Theme.secondary }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 230; clip: true
                TextArea { text: editor.mcpConfiguration; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Consolas"; font.pixelSize: 12 }
            }
            WorkbenchButton { text: "复制 MCP 客户端配置"; enabled: editor.mcpEnabled; onClicked: editor.copyMcpConfiguration() }
            Label { text: editor.mcpActivity; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted }
        }
    }
    footer: Rectangle {
        height: 27; color: Theme.window
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 14; spacing: 12
            Rectangle { width: 6; height: 6; radius: 3; color: editor.meshBusy ? "#e4b763" : Theme.accent }
            Label { text: editor.creationBusy ? "正在更新创作对象" : editor.regionBusy ? editor.regionStatus : editor.status; color: Theme.secondary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
            ProgressBar {objectName:"creationProgressBar";visible:editor.creationBusy;from:0;to:100;value:editor.creationProgress;Layout.preferredWidth:140}
            WorkbenchButton {text:"取消生成";visible:editor.creationBusy && editor.creationCancellable;implicitHeight:28;onClicked:editor.cancelCreation()}
            ProgressBar {objectName:"regionProgressBar";visible:editor.regionBusy;from:0;to:100;value:editor.regionProgress;Layout.preferredWidth:140}
            WorkbenchButton {objectName:"cancelRegionButton";text:"取消区域操作";visible:editor.regionBusy;implicitHeight:28;onClicked:editor.cancelRegion()}
            Label { objectName:"backgroundBuildStatus";text: editor.exportBusy ? "烘焙 " + editor.exportProgress + "%" : editor.meshBusy ? "网格更新中" : "就绪"; color: Theme.accent; font.pixelSize: 11 }
            WorkbenchButton { text: "取消导出"; visible: editor.exportBusy; implicitHeight: 28; onClicked: editor.cancelExport() }
        }
    }
    Shortcut { sequence: win.shortcut("save"); onActivated: win.saveCurrent() }
    Shortcut { sequence: win.shortcut("open"); onActivated: win.requestAction("open") }
    Shortcut { sequence: win.shortcut("new"); onActivated: win.requestAction("new") }
    Shortcut { sequence: win.shortcut("undo"); enabled: !win.editingText() && !editor.commandBusy; onActivated: editor.undo() }
    Shortcut { sequences: [win.shortcut("redo"), "Ctrl+Shift+Z"]; enabled: !win.editingText() && !editor.commandBusy; onActivated: editor.redo() }
    FileDialog {
        id: saveDialog; title: "保存 Worldbench 工程"; fileMode: FileDialog.SaveFile
        nameFilters: ["Worldbench 工程 (*.wbench)"]; defaultSuffix: "wbench"
        onAccepted: { editor.rememberDialogFolder("saveProject",selectedFile); if (editor.save(selectedFile)) win.finishAction() }
        onRejected: { editor.rememberDialogFolder("saveProject",currentFolder,true); win.pendingAction = "" }
    }
    FileDialog { id: openDialog; title: "打开 Worldbench 工程"; fileMode: FileDialog.OpenFile; nameFilters: ["Worldbench 工程 (*.wbench)"]; onAccepted: { editor.rememberDialogFolder("openProject",selectedFile); editor.open(selectedFile) } onRejected: editor.rememberDialogFolder("openProject",currentFolder,true) }
    MessageDialog {
        id: unsaved; objectName:"unsavedDialog"; title: "保存当前改动？"; text: "当前工程有未保存的改动。"
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onRejected:{win.pendingAction="";win.pendingProject=""}
        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Save) win.saveCurrent()
            else if (button === MessageDialog.Discard) win.finishAction()
            else {win.pendingAction = "";win.pendingProject=""}
        }
    }
    MessageDialog { id: errorBox; objectName:"errorDialog"; title: "Worldbench"; buttons: MessageDialog.Ok }
    FolderDialog { id: exportFolder; title: "选择存放新 Minecraft 存档的父目录"; onAccepted: { editor.rememberDialogFolder("exportParent",selectedFolder,true); exportParent.text = selectedFolder.toString() } onRejected: editor.rememberDialogFolder("exportParent",currentFolder,true) }
    Dialog {
        id: exportDialog; title: "导出 Minecraft Java 1.20.1"; modal: true
        anchors.centerIn: parent; width: 520
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: 13
            Label { text: "将当前编辑结果烘焙为独立游戏存档。"; color: Theme.text }
            Label { text: "父目录（可选择 .minecraft/saves）"; color: Theme.secondary }
            RowLayout {
                WorkbenchTextField { id: exportParent; Layout.fillWidth: true; readOnly: true; placeholderText: "选择已有目录…" }
                WorkbenchButton { text: "浏览"; onClicked: { exportFolder.currentFolder=editor.dialogFolder("exportParent"); exportFolder.open() } }
            }
            WorkbenchCheckBox {id:exportSelection;text:"仅导出当前选区（扩展到完整区块）";enabled:editor.selection.valid;checked:editor.selection.valid}
            Label{visible:editor.layerDetails.width>256||editor.layerDetails.depth>256;text:"大地图建议先导出小选区。全图烘焙可能耗时很长，可取消。";color:"#d5ba82";Layout.fillWidth:true;wrapMode:Text.WordWrap}
            Label { text: "新存档名称"; color: Theme.secondary }
            WorkbenchTextField { id: exportName; text: "Worldbench-001"; Layout.fillWidth: true; selectByMouse: true; maximumLength: 64 }
            Label { text: "使用字母、数字、短横线或下划线。已存在的目录不会被覆盖。\nP0：地形范围外为虚空；光照由游戏重新计算。\n资产携带的受支持实体/NBT一并导出；其他版本和自定义高度尚未开放。动态方块需 1.20.1 JAR。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12; color: Theme.secondary }
        }
        onAccepted: editor.exportWorld(exportParent.text, exportName.text,exportSelection.checked&&editor.selection.valid)
    }
    Connections {
        target: editor
        function onResourceSetupRequested() { resourceSetup.open() }
        function onResourceLoadStarted() { win.autoCloseResources=true; settingsContent.page="resources"; settingsDialog.open() }
        function onResourceLoadFinished(success) { if(success) resourceCloseTimer.restart(); else win.autoCloseResources=false }
        function onFailure(message) { errorBox.text = message; errorBox.open() }
        function onExported(path) { errorBox.text = "导出完成：\n" + path + "\n\n请在 Minecraft Java 1.20.1 中测试。"; errorBox.open() }
    }
}
