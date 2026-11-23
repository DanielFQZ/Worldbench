import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

TabButton {
    id: control
    implicitWidth: Math.max(84, implicitContentWidth + 28)
    implicitHeight: 32
    padding: 6
    contentItem: Text {
        text: control.text; font: control.font
        color: control.checked ? Theme.text : control.hovered ? Theme.text : Theme.secondary
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle {
        color: control.checked ? Theme.raised : control.hovered ? Theme.panel : Theme.window
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: control.checked ? Theme.accent : Theme.divider }
    }
}
