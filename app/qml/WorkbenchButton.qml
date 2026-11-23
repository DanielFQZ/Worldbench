import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Button {
    id: control
    property bool primary: false
    implicitWidth: Math.max(28, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: 10; rightPadding: 10; topPadding: 4; bottomPadding: 4
    hoverEnabled: true
    contentItem: Text {
        text: control.text; font: control.font
        color: !control.enabled ? Theme.disabled : Theme.text
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.selectionHover : control.checked || control.primary ? Theme.selection : control.hovered ? Theme.hover : control.flat ? "transparent" : Theme.raised
        border.width: control.flat && !control.checked && !control.activeFocus ? 0 : 1
        border.color: control.activeFocus || control.checked ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : .6
    }
}
