import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Button {
    id: control
    property string symbol: "minimize"
    // Chrome clicks must not retain an editing/keyboard focus ring.
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.name: ToolTip.text
    background: Rectangle {
        radius: 3
        color: control.down ? (control.symbol === "close" ? "#963542" : Theme.selection)
                            : control.hovered ? (control.symbol === "close" ? "#b33b4a" : Theme.hover) : "transparent"
    }
    contentItem: Canvas {
        id: icon
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections { target: control; function onSymbolChanged() { icon.requestPaint() } }
        onPaint: {
            var c = getContext("2d"); c.reset()
            var x = width/2, y = height/2
            c.strokeStyle = Theme.text; c.lineWidth = 1.8; c.lineCap = "square"
            c.beginPath()
            if (control.symbol === "minimize") { c.moveTo(x-5,y+3); c.lineTo(x+5,y+3) }
            else if (control.symbol === "close") { c.moveTo(x-4,y-4); c.lineTo(x+4,y+4); c.moveTo(x+4,y-4); c.lineTo(x-4,y+4) }
            else if (control.symbol === "restore") {
                c.rect(x-5,y-3,8,8); c.moveTo(x-2,y-6); c.lineTo(x+6,y-6); c.lineTo(x+6,y+2)
            } else c.rect(x-5,y-5,10,10)
            c.stroke()
        }
    }
}
