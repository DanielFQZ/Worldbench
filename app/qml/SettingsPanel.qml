import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: panel
    objectName: "settingsPanel"
    property string page: "camera"
    property int pageIndex: page === "camera" ? 0 : page === "performance" ? 1 : page === "shortcuts" ? 2 : 3

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: categoryPane
            Layout.fillHeight: true
            Layout.preferredWidth: 178
            color: "#1b232d"
            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 14
                anchors.bottomMargin: 14
                spacing: 4
                Label { text: "偏好设置"; color: "#e5edf6"; font.pixelSize: 17; font.bold: true; Layout.leftMargin: 18; Layout.bottomMargin: 8 }
                Repeater {
                    model: [
                        { key: "camera", label: "相机与输入", icon: "⌁" },
                        { key: "performance", label: "性能与精度", icon: "◈" },
                        { key: "shortcuts", label: "快捷键", icon: "⌨" },
                        { key: "resources", label: "资源与诊断", icon: "▣" }
                    ]
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        flat: true
                        checkable: true
                        checked: panel.page === modelData.key
                        onClicked: panel.page = modelData.key
                        contentItem: RowLayout {
                            spacing: 10
                            Text { text: modelData.icon; color: parent.parent.checked ? "#8fe6ec" : "#8c9aaa"; font.pixelSize: 16; Layout.preferredWidth: 22; horizontalAlignment: Text.AlignHCenter }
                            Text { text: modelData.label; color: parent.parent.checked ? "#eaffff" : "#b6c2d0"; font.pixelSize: 13; Layout.fillWidth: true }
                        }
                        background: Rectangle {
                            color: parent.checked ? "#25485a" : (parent.hovered ? "#24313e" : "transparent")
                            radius: 4
                            Rectangle { visible: parent.parent.checked; width: 3; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: "#51c6d2" }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Label { text: "本机偏好\n不会写入 .wbench 工程"; color: "#718194"; font.pixelSize: 11; Layout.leftMargin: 18; Layout.bottomMargin: 4 }
            }
        }
        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: "#344252" }
        ScrollView {
            id: contentScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            StackLayout {
                id: pages
                width: contentScroll.availableWidth
                currentIndex: panel.pageIndex
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 12
                    Label { text: "透视相机与输入"; font.pixelSize: 19; color: "#e5edf6"; Layout.margins: 20; Layout.bottomMargin: 0 }
                    Label { text: editor.cameraController ? editor.cameraController.cameraText : ""; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#a6b4c5" }
                    CheckBox { text: "Windows 原始鼠标输入"; checked: editor.cameraController ? editor.cameraController.rawInput : false; Layout.leftMargin: 20; onToggled: if (editor.cameraController) editor.cameraController.rawInput = checked }
                    CheckBox { text: "飞行惯性（平滑加速 / 减速）"; checked: editor.cameraController ? editor.cameraController.flightInertia : true; Layout.leftMargin: 20; onToggled: if (editor.cameraController) editor.cameraController.flightInertia = checked }
                    Label { text: "松开方向键时短暂减速；松开右键、Esc 或切换窗口立即停止。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#9caabb" }
                    Label { text: "旋转灵敏度  " + (editor.cameraController ? editor.cameraController.sensitivity.toFixed(2) : "") + " °/计数"; color: "#a6b4c5"; Layout.leftMargin: 20 }
                    Slider { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; from: .01; to: .5; stepSize: .01; value: editor.cameraController ? editor.cameraController.sensitivity : .12; onMoved: if (editor.cameraController) editor.cameraController.sensitivity = value }
                    Label { text: editor.cameraController && editor.cameraController.rawActive ? "原始输入：捕获中" : "原始输入：导航时启用"; color: "#74c2bd"; Layout.leftMargin: 20 }
                    Label { text: "已接收原始输入包：" + (editor.cameraController ? editor.cameraController.rawPackets : 0); color: "#9caabb"; Layout.leftMargin: 20 }
                    Button { text: "重置视角"; Layout.leftMargin: 20; onClicked: if (editor.cameraController) editor.cameraController.resetCamera() }
                    Label { text: "原始输入绕过系统指针加速。松开鼠标、Esc 或切出窗口会释放捕获。关闭时使用普通相对拖动，受桌面边界限制。"; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; color: "#8997a9" }
                    Item { Layout.preferredHeight: 20 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 12
                    Label { text: "性能与精度"; font.pixelSize: 19; color: "#e5edf6"; Layout.margins: 20; Layout.bottomMargin: 0 }
                    CheckBox { text: "4× 多重采样抗锯齿"; checked: editor.cameraController ? editor.cameraController.antialiasing : true; Layout.leftMargin: 20; onToggled: if (editor.cameraController) editor.cameraController.antialiasing = checked }
                    Label { text: "使用视口原生多重采样平滑体素轮廓和水面边缘。关闭后使用单采样，可在低端设备上节省显存与带宽。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#91a8ba" }
                    Label { text: "全精度区域半径（区块）"; color: "#dce4ee"; Layout.leftMargin: 20 }
                    RowLayout { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        Slider { id: exactRadius; from: 1; to: 12; stepSize: 1; snapMode: Slider.SnapAlways; value: editor.exactRadiusChunks; Layout.fillWidth: true; onMoved: editor.exactRadiusChunks = Math.round(value) }
                        Label { text: editor.exactRadiusChunks + " · 约 " + (editor.exactRadiusChunks * 2 + 1) + "×" + (editor.exactRadiusChunks * 2 + 1) + " 区块"; Layout.preferredWidth: 180; color: "#78c9d2" }
                    }
                    Label { text: "范围越大，近处真实方块细节越多，同时构网、显存和后台线程开销也会增加。远景使用误差约束的连续地表。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#91a8ba" }
                    Label { text: "交互预览"; font.pixelSize: 15; color: "#dce4ee"; Layout.leftMargin: 20 }
                    Label { text: "地貌实例拖动期间发布完整的低精度预览快照，保证轮廓实时更新且不混入旧碎片；松开后合并脏区域并只进行一次正式高精度构建。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#91a8ba" }
                    Label { text: "当前构网：" + editor.meshUpdateSummary; color: "#a6b4c5"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
                    Item { Layout.preferredHeight: 20 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Label { text: "快捷键绑定"; font.pixelSize: 19; color: "#e5edf6"; Layout.margins: 20; Layout.bottomMargin: 0 }
                    Label { text: "修改后立即保存为本机偏好；输入 Qt 支持的组合键，例如 Ctrl+Shift+S。视口内的 W/E/R 也会同步用于操纵器切换。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#91a8ba" }
                    ListModel { id: shortcutModel
                        ListElement { action: "save"; label: "保存工程" }
                        ListElement { action: "open"; label: "打开工程" }
                        ListElement { action: "new"; label: "新建工程" }
                        ListElement { action: "undo"; label: "撤销" }
                        ListElement { action: "redo"; label: "重做" }
                        ListElement { action: "clearSelection"; label: "清除选区" }
                        ListElement { action: "deleteTerrain"; label: "删除地貌实例" }
                        ListElement { action: "focus"; label: "聚焦" }
                        ListElement { action: "translateGizmo"; label: "位移操纵器" }
                        ListElement { action: "rotateGizmo"; label: "旋转操纵器" }
                        ListElement { action: "scaleGizmo"; label: "缩放操纵器" }
                        ListElement { action: "toolCycle"; label: "抬升 / 连续或长按循环工具" }
                        ListElement { action: "toolFlatten"; label: "整平地表" }
                        ListElement { action: "toolCarve"; label: "体积挖除" }
                        ListElement { action: "toolFill"; label: "体积填充" }
                        ListElement { action: "toolPaint"; label: "材质涂绘" }
                        ListElement { action: "toolSmooth"; label: "平滑地形" }
                        ListElement { action: "toolSelect"; label: "矩形选区" }
                        ListElement { action: "toolNavigate"; label: "选择 / 导航" }
                    }
                    Repeater { model: shortcutModel; delegate: RowLayout { required property string action; required property string label; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        Label { text: label; Layout.fillWidth: true; color: "#c9d5e2" }
                        TextField {
                            id: shortcutInput
                            property string key: action
                            text: editor.shortcut(key); selectByMouse: true; Layout.preferredWidth: 170
                            onEditingFinished: editor.setShortcut(key, text)
                            Connections { target: editor; function onShortcutsChanged() { shortcutInput.text = editor.shortcut(shortcutInput.key) } }
                        }
                    }}
                    Button { text: "恢复默认快捷键"; Layout.leftMargin: 20; onClicked: editor.resetShortcuts() }
                    Item { Layout.preferredHeight: 20 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 12
                    Label { text: "Minecraft 资源与导入诊断"; font.pixelSize: 19; color: "#e5edf6"; Layout.margins: 20; Layout.bottomMargin: 0 }
                    Label { text: editor.assetStage; visible: editor.assetStage.length > 0; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; color: "#76d3e2" }
                    ProgressBar { id: resourceProgress; objectName: "resourceProgress"; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; visible: editor.assetStage.length > 0; from: 0; to: 100; value: editor.assetProgress
                        background: Rectangle { implicitHeight: 8; color: "#131d28"; radius: 4 }
                        contentItem: Item { implicitHeight: 8; Rectangle { width: parent.width * resourceProgress.visualPosition; height: parent.height; color: "#43bace"; radius: 4 } }
                    }
                    Label { visible: editor.assetBusy; text: "解析进度 " + editor.assetProgress + "% · 可以移动或关闭此窗口，后台任务会继续"; color: "#a5bacb"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
                    ScrollView { visible: editor.assetLog.length > 0; Layout.fillWidth: true; Layout.preferredHeight: 180; Layout.leftMargin: 20; Layout.rightMargin: 20; clip: true
                        TextArea { objectName: "resourceLiveLog"; text: editor.assetLog; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.pixelSize: 11; color: "#a8c6d8"; background: Rectangle { color: "#151f2a" }
                            onTextChanged: cursorPosition = length
                        }
                    }
                    Label { text: editor.defaultResourceSources.length ? "默认资源：" + editor.defaultResourceSources.join("\n") : "尚未设置默认资源"; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WrapAnywhere; color: "#91a8ba"; font.pixelSize: 11 }
                    Button { text: "清除默认资源设置"; enabled: !editor.assetBusy && editor.defaultResourceSources.length > 0; Layout.leftMargin: 20; onClicked: editor.clearDefaultResources() }
                    Label { text: "手动成功导入或移除来源后会记为默认资源。打开工程优先使用工程自带来源；清除默认设置不移除当前工程资源。"; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; color: "#91a8ba"; font.pixelSize: 11 }
                    Label { text: editor.resourceSummary; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#a6b4c5" }
                    Repeater { model: editor.resourceSources; delegate: ColumnLayout { required property string modelData; required property int index; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        Label { text: (index === 0 ? "基础 · " : "覆盖 " + index + " · ") + modelData; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 11; color: "#c4ccd8" }
                        Button { text: index === 0 ? "清除所有来源" : "移除此覆盖包"; enabled: !editor.assetBusy && !editor.exportBusy; onClicked: editor.removeResource(index) }
                    }}
                    Label { text: editor.resourceDiagnostics; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; color: "#d5ba82"; font.pixelSize: 11 }
                    Label { text: "从顶部“资源”菜单载入客户端 JAR，然后叠加 ZIP。后载入的包优先。\n专用实体渲染模型、动画及模组格式尚未支持；缺失模型显示洋红色方块。"; color: "#8997a9"; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap }
                    Item { Layout.preferredHeight: 20 }
                }
            }
        }
    }
}
