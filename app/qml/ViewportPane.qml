import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Worldbench 1.0
import "Theme.js" as Theme

Rectangle {
    id:root
    anchors.fill: parent
    color: Theme.window
    property var controller: editor
    signal assetRequested(string id)
    signal importRequested()
    signal graphRequested(bool material)
    function focusViewport(){viewport.forceActiveFocus()}
    ColumnLayout {
        anchors.fill:parent;spacing:1
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:34;color:Theme.panel
            RowLayout {
                anchors.fill:parent;anchors.leftMargin:12;anchors.rightMargin:8;spacing:3
                Label{text:"透视";color:Theme.text;font.pixelSize:12}
                Rectangle{Layout.preferredWidth:1;Layout.preferredHeight:16;color:Theme.border;Layout.leftMargin:8;Layout.rightMargin:8}
                Label{text:editor.layerDetails.width+" × "+editor.layerDetails.depth;color:Theme.muted;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideRight}
                Repeater {
                    model:[{glyph:"move",hint:"位移 · W"},{glyph:"rotate",hint:"旋转 · E"},{glyph:"scale",hint:"局部轴缩放 · R"}]
                    ViewportIconButton {required property var modelData;required property int index;objectName:"transformMode"+index;glyph:modelData.glyph;hint:modelData.hint;visible:editor.terrainDetails.valid||editor.creationDetails.valid;checkable:true;checked:viewport.gizmoMode===index;onClicked:{editor.tool=-1;viewport.gizmoMode=index;viewport.forceActiveFocus()}}
                }
                ViewportIconButton {objectName:"snapToggle";glyph:"magnet";hint:"变换吸附 · 位移4格 / 旋转15° / 缩放10%（Ctrl临时开启）";checkable:true;checked:viewport.snapEnabled;onClicked:{viewport.snapEnabled=!viewport.snapEnabled;viewport.forceActiveFocus()}}
                Rectangle{Layout.preferredWidth:1;Layout.preferredHeight:16;color:Theme.border;Layout.margins:5}
                ViewportIconButton {objectName:"environmentToggle";glyph:"sun";hint:"环境光照预览 · 参数见环境组件";checkable:true;checked:editor.environmentDetails.enabled;onClicked:editor.setEnvironment({enabled:!editor.environmentDetails.enabled})}
                ViewportIconButton {objectName:"frameWorld";glyph:"map";hint:"显示全图 · 双击远处地表进入精确编辑";onClicked:{viewport.frameWorld();viewport.forceActiveFocus()}}
                ViewportIconButton {glyph:"focus";hint:"聚焦 · F";onClicked:{viewport.focusTarget();viewport.forceActiveFocus()}}
                ViewportIconButton {glyph:"reset";hint:"重置视角";onClicked:{viewport.resetCamera();viewport.forceActiveFocus()}}
                ViewportIconButton {objectName:"viewportStatsToggle";glyph:"stats";hint:"显示 / 隐藏渲染统计";checkable:true;checked:diagnostics.visible;onClicked:diagnostics.visible=!diagnostics.visible}
            }
        }
        Item {
            Layout.fillWidth:true;Layout.fillHeight:true
            VoxelViewport {id:viewport;objectName:"voxelViewport";anchors.fill:parent;editor:root.controller;focus:true}
            CreationBoundsOverlay {anchors.fill:parent;viewport:viewport}
            GizmoOverlay {anchors.fill:parent;viewport:viewport}
            Rectangle {
                id:diagnostics;objectName:"viewportDiagnostics";visible:false
                anchors.bottom:parent.bottom;anchors.left:parent.left;anchors.right:parent.right
                height:96;color:"#e6202020"
                Column {
                    anchors.fill:parent;anchors.margins:10;spacing:4
                    Text {objectName:"terrainUpdateStatus";text:editor.terrainUpdateStatus;color:Theme.secondary;font.pixelSize:11;width:parent.width;elide:Text.ElideRight}
                    Text {text:viewport.hitText;color:Theme.text;font.pixelSize:11;width:parent.width;elide:Text.ElideRight}
                    Text {text:viewport.renderText;color:Theme.secondary;font.pixelSize:11;width:parent.width;elide:Text.ElideRight}
                    Text {text:viewport.cameraText+" · "+editor.meshUpdateSummary;color:Theme.muted;font.pixelSize:11;width:parent.width;elide:Text.ElideRight}
                }
            }
        }
        AssetShelf {id:assetShelf;objectName:"assetShelf";Layout.fillWidth:true;Layout.preferredHeight:expanded?196:30;viewport:viewport;onEditorRequested:id=>root.assetRequested(id);onImportRequested:root.importRequested();onGraphRequested:material=>root.graphRequested(material)}
    }
}
