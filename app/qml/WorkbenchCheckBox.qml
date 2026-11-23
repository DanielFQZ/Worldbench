import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

CheckBox {
    id: control
    implicitHeight: Math.max(28, implicitContentHeight + topPadding + bottomPadding)
    padding: 4; spacing: 8
    indicator: Rectangle {
        implicitWidth: 16; implicitHeight: 16
        x: control.leftPadding; y: (control.height-height)/2; radius: 3
        color: control.checked ? Theme.selection : Theme.field
        border.color: control.activeFocus || control.checked ? Theme.accent : control.hovered ? Theme.secondary : Theme.border
        Text { anchors.centerIn: parent; text: "✓"; visible: control.checked; color: Theme.text; font.pixelSize: 13 }
        opacity: control.enabled ? 1 : .5
    }
    contentItem: Text {
        text: control.text; font: control.font
        color: control.enabled ? Theme.text : Theme.disabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
        elide: Text.ElideRight
    }
}
