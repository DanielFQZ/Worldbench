import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Worldbench 1.0

Rectangle {
    anchors.fill: parent
    color: "#202c3b"
    property var controller: editor
    VoxelViewport { id: viewport; objectName: "voxelViewport"; anchors.fill: parent; editor: parent.controller; focus: true }
    GizmoOverlay {anchors.fill:parent;viewport:viewport}
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 43; color: "#dd17212d"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 10
            Label { text: "透视 / 地形"; color: "#e4edf6"; font.pixelSize: 12 }
            Label { text: "  ·  " + editor.sceneStats; color: "#8297ad"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Repeater {
                model:[{glyph:"move",hint:"位移 · W"},{glyph:"rotate",hint:"旋转 · E"},{glyph:"scale",hint:"局部轴缩放 · R"}]
                ViewportIconButton { required property var modelData;required property int index;objectName:"transformMode"+index;glyph:modelData.glyph;hint:modelData.hint;visible:editor.terrainDetails.valid;checkable:true;checked:viewport.gizmoMode===index;onClicked:{editor.tool=-1;viewport.gizmoMode=index;viewport.forceActiveFocus()} }
            }
            ViewportIconButton {objectName:"snapToggle";glyph:"magnet";hint:"变换吸附 · 位移4格 / 旋转15° / 缩放10%（Ctrl临时开启）";checkable:true;checked:viewport.snapEnabled;onClicked:{viewport.snapEnabled=!viewport.snapEnabled;viewport.forceActiveFocus()}}
            ViewportIconButton {objectName:"environmentToggle";glyph:"sun";hint:"环境光照预览（详细参数见环境组件）";checkable:true;checked:editor.environmentDetails.enabled;onClicked:editor.setEnvironment({enabled:!editor.environmentDetails.enabled})}
            ViewportIconButton {objectName:"frameWorld";glyph:"map";hint:"显示全图 · 双击远处地表进入精确编辑";onClicked:{viewport.frameWorld();viewport.forceActiveFocus()}}
            ViewportIconButton {glyph:"focus";hint:"聚焦 · F";onClicked:{viewport.focusTarget();viewport.forceActiveFocus()}}
            ViewportIconButton {glyph:"reset";hint:"重置视角";onClicked:{viewport.resetCamera();viewport.forceActiveFocus()}}
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 99; color: "#d9161f2b"
        Column {
            anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 5
            Text { objectName:"terrainUpdateStatus"; text:editor.terrainUpdateStatus; color:editor.meshBusy?"#a6c6d8":"#8e9fb2";font.pixelSize:11;font.family:"Microsoft YaHei UI" }
            Text { text: viewport.hitText; color: "#8cdae5"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
            Text { text: viewport.renderText; color:"#8e9fb2";font.pixelSize:11;font.family:"Microsoft YaHei UI" }
            Text { text: viewport.cameraText+"   ·   "+editor.meshUpdateSummary; color: "#8e9fb2"; font.pixelSize: 11; font.family: "Microsoft YaHei UI" }
        }
    }
}
