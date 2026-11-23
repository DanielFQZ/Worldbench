import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

TextField {
    id: control
    implicitHeight: Theme.controlHeight
    leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
    color: enabled ? Theme.text : Theme.disabled
    placeholderTextColor: Theme.muted
    selectionColor: Theme.selectionHover; selectedTextColor: Theme.text
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radius; color: Theme.field
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
