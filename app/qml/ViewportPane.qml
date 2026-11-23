import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Worldbench 1.0

Rectangle {
    anchors.fill: parent
    color: "#202c3b"
    property var controller: editor
    VoxelViewport { id: viewport; objectName: "voxelViewport"; anchors.fill: parent; editor: parent.controller; focus: true }
    CreationOverlay {anchors.fill:parent;viewport:viewport}
    GizmoOverlay {anchors.fill:parent;viewport:viewport}
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 43; color: "#dd17212d"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 10
            Label { text: "透视 / 地形"; color: "#e4edf6"; font.pixelSize: 12 }
            Label { text: "  ·  " + editor.sceneStats; color: "#8297ad"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Repeater {
                model:[{glyph:"move",hint:"位移 · W"},{glyph:"rotate",hint:"旋转 · E"},{glyph:"scale",hint:"局部轴缩放 · R"}]
                ViewportIconButton { required property var modelData;required property int index;objectName:"transformMode"+index;glyph:modelData.glyph;hint:modelData.hint;visible:editor.terrainDetails.valid||editor.creationDetails.valid;checkable:true;checked:viewport.gizmoMode===index;onClicked:{editor.tool=-1;viewport.gizmoMode=index;viewport.forceActiveFocus()} }
            }
            ViewportIconButton {objectName:"snapToggle";glyph:"magnet";hint:"变换吸附 · 位移4格 / 旋转15° / 缩放10%（Ctrl临时开启）";checkable:true;checked:viewport.snapEnabled;onClicked:{viewport.snapEnabled=!viewport.snapEnabled;viewport.forceActiveFocus()}}
            Button {objectName:"assetShelfToggle";text:"▣";implicitWidth:30;implicitHeight:30;checkable:true;checked:assetShelf.visible;onClicked:assetShelf.visible=!assetShelf.visible;ToolTip.visible:hovered;ToolTip.text:"资产快捷库 · 拖入视口放置"}
            ViewportIconButton {objectName:"environmentToggle";glyph:"sun";hint:"环境光照预览（详细参数见环境组件）";checkable:true;checked:editor.environmentDetails.enabled;onClicked:editor.setEnvironment({enabled:!editor.environmentDetails.enabled})}
            ViewportIconButton {objectName:"frameWorld";glyph:"map";hint:"显示全图 · 双击远处地表进入精确编辑";onClicked:{viewport.frameWorld();viewport.forceActiveFocus()}}
            ViewportIconButton {glyph:"focus";hint:"聚焦 · F";onClicked:{viewport.focusTarget();viewport.forceActiveFocus()}}
            ViewportIconButton {glyph:"reset";hint:"重置视角";onClicked:{viewport.resetCamera();viewport.forceActiveFocus()}}
        }
    }
    Rectangle {
        id:assetShelf;objectName:"assetShelf";visible:false;anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:12;anchors.bottomMargin:110;width:260;height:260;color:"#ee202c3b";border.color:"#4d6879";radius:6
        ColumnLayout {anchors.fill:parent;anchors.margins:8
            Label{text:"资产快捷库 · 拖动放置";color:"#dce8f0"}
            ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:editor.structureAssets
                delegate:Rectangle {id:assetRow;required property var modelData;width:ListView.view.width;height:52;color:"#344957";border.color:"#202c3b"
                    Text {anchors.centerIn:parent;width:parent.width-16;text:assetRow.modelData.name;elide:Text.ElideRight;color:"#e1ecf4"}
                    property string assetId:modelData.id
                    MouseArea {
                        id:assetDrag;anchors.fill:parent;preventStealing:true
                        property point startPoint
                        property bool dragging:false
                        onPressed:function(mouse){startPoint=Qt.point(mouse.x,mouse.y);dragging=false}
                        onPositionChanged:function(mouse){if(pressed&&Math.hypot(mouse.x-startPoint.x,mouse.y-startPoint.y)>8)dragging=true}
                        cursorShape:dragging?Qt.DragCopyCursor:Qt.OpenHandCursor
                        onReleased:function(mouse){
                            if(dragging){let p=mapToItem(viewport,mouse.x,mouse.y),s=mapToItem(assetShelf,mouse.x,mouse.y);
                                if(p.x>=0&&p.x<viewport.width&&p.y>=43&&p.y<viewport.height-99&&!(s.x>=0&&s.x<assetShelf.width&&s.y>=0&&s.y<assetShelf.height)){viewport.dropAsset(assetRow.assetId,p.x,p.y);viewport.forceActiveFocus()}
                            }
                            dragging=false
                        }
                        onCanceled:dragging=false
                        onDoubleClicked:{editor.beginPlacement(assetRow.assetId);assetShelf.visible=false}
                    }
                }
                ScrollBar.vertical:ScrollBar{}
            }
            Label{text:editor.structureAssets.length?"双击后点击地表也可放置":"在「创作」工作区导入结构";color:"#9eb5c9"}
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 99; color: "#d9161f2b"
        Column {
            anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 5
            Text { objectName:"terrainUpdateStatus"; text:editor.terrainUpdateStatus; color:editor.meshBusy?"#a6c6d8":"#8e9fb2";font.pixelSize:11;font.family:"Microsoft YaHei UI" }
            Text { text: viewport.hitText; color: "#8cdae5"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
            Text { text: viewport.renderText; color:"#8e9fb2";font.pixelSize:11;font.family:"Microsoft YaHei UI" }
            Text { text: viewport.cameraText+"   ·   "+editor.meshUpdateSummary; color: "#8e9fb2"; font.pixelSize: 11; font.family: "Microsoft YaHei UI" }
        }
    }
}
