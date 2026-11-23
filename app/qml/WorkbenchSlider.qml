import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Slider {
    id: control
    implicitHeight: 26; implicitWidth: 160
    leftPadding: 6; rightPadding: 6
    background: Rectangle {
        x: control.leftPadding; y: (control.height-height)/2
        width: control.availableWidth; height: 4; radius: 2; color: Theme.field
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent; opacity: control.enabled ? .8 : .3 }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth-width)
        y: (control.height-height)/2
        implicitWidth: 10; implicitHeight: 16; radius: 2
        color: control.pressed || control.hovered ? Theme.text : Theme.secondary
        border.color: control.activeFocus ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : .4
    }
}
