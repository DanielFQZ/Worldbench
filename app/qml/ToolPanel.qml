import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property size kddockwidgets_min_size: Qt.size(230, 380)
    color: "#202630"; implicitWidth: 255; implicitHeight: 600
    ScrollView {
        anchors.fill: parent; anchors.margins: 16; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 12
            Label { text: "SCULPT"; font.pixelSize: 11; font.letterSpacing: 2; color: "#6faabb" }
            Label { text: "雕刻地形"; font.pixelSize: 21; font.bold: true; color: "#edf2f8"; Layout.bottomMargin: 4 }
            Repeater {
                model: ["抬升 / 降低", "整平地表", "体积挖除", "体积填充", "材质涂绘"]
                delegate: Button {
                    required property int index; required property string modelData
                    Layout.fillWidth: true; implicitHeight: 38
                    text: modelData; highlighted: editor.tool === index; onClicked: editor.tool = index
                    background: Rectangle {
                        radius: 3
                        color: editor.tool === index ? "#254b59" : "#303b4b"
                        border.width: 1; border.color: editor.tool === index ? "#51b8ca" : "#3b4657"
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#364150"; Layout.topMargin: 8; Layout.bottomMargin: 5 }
            RowLayout {
                Label { text: "笔刷半径"; color: "#c2ccd9"; Layout.fillWidth: true }
                Label { text: editor.radius + " 格"; color: "#74cbd8" }
            }
            Slider { Layout.fillWidth: true; from: 1; to: 16; stepSize: 1; value: editor.radius; onMoved: editor.radius = value }
            RowLayout {
                Label { text: "高度步进"; color: editor.tool < 2 ? "#c2ccd9" : "#67798d"; Layout.fillWidth: true }
                Label { text: editor.strength + " 格"; color: "#74cbd8" }
            }
            Slider { Layout.fillWidth: true; from: 1; to: 4; stepSize: 1; value: editor.strength; enabled: editor.tool < 2; onMoved: editor.strength = value }
            Label { text: "填充 / 涂绘材质"; color: "#c2ccd9"; Layout.topMargin: 8 }
            ComboBox {
                Layout.fillWidth: true
                model: ["草方块", "泥土", "石头", "沙子", "水", "橡木木板", "玻璃", "橡木半砖"]
                currentIndex: editor.material - 1; onActivated: editor.material = currentIndex + 1
            }
            Label {
                Layout.fillWidth: true; Layout.topMargin: 10; wrapMode: Text.WordWrap; font.pixelSize: 12; lineHeight: 1.5
                color: "#8e9fb3"
                text: editor.tool === 0 ? "左键抬升地表，Shift + 左键降低。按住可连续应用。" : editor.tool === 1 ? "按下笔划时采样高度，将周围地表逐渐整平到该高度。" : editor.tool === 2 ? "在命中位置挖除球形体积。Shift 反转为填充。" : editor.tool === 3 ? "在命中表面外填充球形体积。Shift 反转为挖除。" : "将球形范围内已有方块替换为选中材质，不填充空气。"
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "每个完整笔划为一次撤销。\n[ / ] 调整半径。底层基岩受保护。"; font.pixelSize: 11; lineHeight: 1.5; color: "#74879e" }
            Item { implicitHeight: 20 }
        }
    }
}
