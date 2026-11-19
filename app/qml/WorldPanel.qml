import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property size kddockwidgets_min_size: Qt.size(230, 380)
    color: "#202630"; implicitWidth: 260; implicitHeight: 600
    ScrollView {
        anchors.fill: parent; anchors.margins: 17; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 14
            Label { text: "WORKSPACE"; font.pixelSize: 11; font.letterSpacing: 2; color: "#6faabb" }
            Label { text: "世界概览"; font.pixelSize: 21; font.bold: true; color: "#edf2f8" }
            Label { text: editor.projectName + (editor.modified ? " · 未保存" : ""); color: "#c9d6e3"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: "高度  −64 … 319\n导出  Java 1.20.1\n工程  .wbench / v1"; lineHeight: 1.8; font.pixelSize: 12; color: "#91a8bc" }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#364150" }
            Label { text: "UE 式视口导航"; font.pixelSize: 14; font.bold: true; color: "#d8e3ed" }
            Repeater {
                model: ["右键拖动      环视", "右键 + WASD   前后左右", "右键 + Q / E    下降 / 上升", "右键 + 滚轮    飞行速度", "Shift             加速飞行", "中键 / 双键     平移", "Alt + 左键      围绕焦点旋转", "Alt + 中键      平移", "Alt + 右键      推拉", "滚轮 / F         推拉 / 聚焦", "Ctrl + Z / Y     撤销 / 重做", "Ctrl + S           保存工程"]
                delegate: Label { required property string modelData; text: modelData; color: "#a1b2c5"; font.pixelSize: 11 }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#364150"; Layout.topMargin: 6 }
            Label { text: "P0 测试边界"; color: "#d3b57a"; font.pixelSize: 13 }
            Label { text: "默认 128 × 128 场景。\n尚未包含大地图 LOD、复杂资产、路网、其他版本或超高世界。\n透明材质为简化预览；不模拟游戏光照与流体更新。"; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; font.pixelSize: 11; color: "#8598ad" }
            Item { implicitHeight: 12 }
        }
    }
}
