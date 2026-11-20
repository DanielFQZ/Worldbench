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
            Label { text: "透视  /  体素"; color: "#e4edf6"; font.pixelSize: 12 }
            Label { text: "  ·  " + editor.sceneStats; color: "#8297ad"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Repeater {
                model:["位移 W","旋转 E","缩放 R"]
                Button { required property string modelData;required property int index;text:modelData;visible:editor.terrainDetails.valid;implicitHeight:27;checkable:true;checked:viewport.gizmoMode===index;onClicked:{editor.tool=-1;viewport.gizmoMode=index;viewport.forceActiveFocus()} }
            }
            Button { text: "聚焦 F"; implicitHeight: 27; onClicked: { viewport.focusTarget(); viewport.forceActiveFocus() } }
            Button { text: "重置视角"; implicitHeight: 27; onClicked: { viewport.resetCamera(); viewport.forceActiveFocus() } }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 59; color: "#d9161f2b"
        Column {
            anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 5
            Text { text: viewport.hitText; color: "#8cdae5"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
            Text { text: viewport.cameraText+"   ·   "+editor.meshUpdateSummary; color: "#8e9fb2"; font.pixelSize: 11; font.family: "Microsoft YaHei UI" }
        }
    }
    Rectangle {
        visible: editor.meshBusy && editor.sceneStats.indexOf("0 区块") >= 0
        anchors.centerIn: parent; width: 260; height: 70; radius: 8; color: "#e91b2633"
        Label { anchors.centerIn: parent; text: "正在构建初始体素网格…"; color: "#d6e7f1" }
    }
}
