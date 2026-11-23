import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

Rectangle {
    id:root
    property var viewport
    property bool expanded:false
    signal editorRequested()
    color:Theme.window
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:30;color:Theme.panel
            RowLayout {
                anchors.fill:parent;anchors.leftMargin:6;anchors.rightMargin:8;spacing:8
                WorkbenchButton {objectName:"assetShelfToggle";text:(root.expanded?"▾  ":"▸  ")+"资产库";flat:true;implicitHeight:28;checkable:true;checked:root.expanded;onClicked:root.expanded=!root.expanded}
                Label{text:editor.structureAssets.length+" 项";color:Theme.muted;font.pixelSize:11}
                Item{Layout.fillWidth:true}
                WorkbenchTextField {id:search;visible:root.expanded;Layout.preferredWidth:180;implicitHeight:24;placeholderText:"搜索资产…"}
                WorkbenchButton {visible:root.expanded;text:"管理资产 ↗";flat:true;implicitHeight:26;onClicked:root.editorRequested()}
            }
        }
        Item {
            visible:root.expanded;Layout.fillWidth:true;Layout.fillHeight:true
            GridView {
                id:grid;objectName:"assetShelfGrid";anchors.fill:parent;anchors.margins:10
                clip:true;cellWidth:150;cellHeight:142
                model:root.expanded?editor.structureAssets.filter(a=>!search.text||a.name.toLowerCase().indexOf(search.text.toLowerCase())>=0):[]
                ScrollBar.vertical:ScrollBar{}
                delegate:Rectangle {
                    id:card;required property var modelData
                    width:140;height:132;radius:3;color:drag.hovered?Theme.hover:Theme.panel;border.color:drag.dragging?Theme.accent:Theme.border
                    Column {
                        anchors.fill:parent;anchors.margins:9;spacing:5
                        Rectangle {
                            width:parent.width;height:70;color:Theme.field;radius:2
                            Canvas {
                                anchors.centerIn:parent;width:60;height:54
                                onPaint:{let c=getContext("2d");c.reset();c.strokeStyle=Theme.secondary;c.lineWidth=1.3;c.beginPath();c.moveTo(30,5);c.lineTo(53,17);c.lineTo(53,39);c.lineTo(30,51);c.lineTo(7,39);c.lineTo(7,17);c.closePath();c.moveTo(7,17);c.lineTo(30,29);c.lineTo(53,17);c.moveTo(30,29);c.lineTo(30,51);c.stroke()}
                            }
                        }
                        Text {text:card.modelData.name;width:parent.width;elide:Text.ElideRight;color:Theme.text;font.pixelSize:12}
                        Text {text:card.modelData.width+" × "+card.modelData.height+" × "+card.modelData.depth;width:parent.width;elide:Text.ElideRight;color:Theme.muted;font.pixelSize:10}
                    }
                    MouseArea {
                        id:drag;anchors.fill:parent;hoverEnabled:true;preventStealing:true
                        property point startPoint
                        property bool dragging:false
                        readonly property bool hovered:containsMouse
                        cursorShape:dragging?Qt.DragCopyCursor:Qt.OpenHandCursor
                        onPressed:function(mouse){startPoint=Qt.point(mouse.x,mouse.y);dragging=false}
                        onPositionChanged:function(mouse){if(pressed&&Math.hypot(mouse.x-startPoint.x,mouse.y-startPoint.y)>8)dragging=true}
                        onReleased:function(mouse){
                            if(dragging&&root.viewport){let p=mapToItem(root.viewport,mouse.x,mouse.y);if(p.x>=0&&p.x<root.viewport.width&&p.y>=0&&p.y<root.viewport.height){root.viewport.dropAsset(card.modelData.id,p.x,p.y);root.viewport.forceActiveFocus()}}
                            dragging=false
                        }
                        onCanceled:dragging=false
                        onDoubleClicked:{editor.beginPlacement(card.modelData.id);root.viewport.forceActiveFocus()}
                    }
                    ToolTip.visible:drag.containsMouse&&!drag.pressed;ToolTip.delay:700;ToolTip.text:card.modelData.name+" · 拖入地表放置，或双击后点击地表"
                }
            }
            Column {
                anchors.centerIn:parent;spacing:10;visible:grid.count===0
                Label {anchors.horizontalCenter:parent.horizontalCenter;text:search.text?"没有匹配的资产":"将结构、树木和洞穴收纳到资产库";color:Theme.secondary}
                WorkbenchButton {anchors.horizontalCenter:parent.horizontalCenter;visible:!search.text;text:"导入第一个资产";onClicked:root.editorRequested()}
            }
        }
    }
}
