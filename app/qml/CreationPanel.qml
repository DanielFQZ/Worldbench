import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Worldbench 1.0

Pane {
 id:panel;objectName:"creationPanel";padding:0
 property var controller:editor
 property var asset:({})
 readonly property string kind:asset.kind || ""
 readonly property bool rangePreset:kind==="range" || kind==="materialRange"
 readonly property bool graph:rangePreset || ["graph","material"].includes(kind)
 signal placeRequested()
 signal assetRequested(string id)
 background:Rectangle{color:Theme.panel}
 ColumnLayout {
  anchors.fill:parent;spacing:0
  Label{Layout.fillWidth:true;Layout.margins:8;visible:(editor.creationState.diagnostics||[]).length>0;text:(editor.creationState.diagnostics||[]).join("\n");color:Theme.warning;wrapMode:Text.WordWrap}
  Loader {
   Layout.fillWidth:true;Layout.fillHeight:true
   sourceComponent:panel.graph?graphPage:panel.kind==="terrain"?terrainPage:panel.kind==="road"?roadPage:
                   ["open","closed"].includes(panel.kind)?structurePage:null
  }
 }
 Component {
  id:graphPage
  ColumnLayout {
   RowLayout {Layout.fillWidth:true;Layout.margins:8
    Label{text:panel.kind==="materialRange"?"地表材质范围预设":panel.kind==="material"?"地表材质":panel.kind==="range"?"PCG 范围预设":"PCG 节点图";color:Theme.secondary}
    WorkbenchTextField {Layout.fillWidth:true;text:panel.asset.name||"";maximumLength:128
     onEditingFinished:if(text!==panel.asset.name && !editor.renameLibraryAsset(panel.asset.id,text))text=panel.asset.name
    }
    WorkbenchButton {text:"在地表放置范围";onClicked:{editor.beginPlacement(panel.asset.id);panel.placeRequested()}}
   }
   PcgGraphEditor {Layout.fillWidth:true;Layout.fillHeight:true;graphId:panel.asset.id||"";rangePreset:panel.rangePreset;onGraphImported:id=>panel.assetRequested(id)}
  }
 }
 Component {id:structurePage;AssetEditor {assetId:panel.asset.id||"";kind:panel.kind;onPlaceRequested:panel.placeRequested()}}
 Component {
  id:terrainPage
  SplitView {
   Item {
    SplitView.fillWidth:true
    VoxelViewport {id:terrainView;objectName:"terrainAssetViewport";anchors.fill:parent;editor:panel.controller.assetInspector}
    ViewportIconButton {
     x:12;y:12;glyph:"sun";hint:"地貌光照预览";checkable:true;checked:panel.controller.assetInspector.environmentDetails.enabled
     onClicked:panel.controller.assetInspector.setEnvironment({enabled:!panel.controller.assetInspector.environmentDetails.enabled})
    }
    property var terrain:panel.asset
    function refresh(){if(terrain && terrain.id && visible)panel.controller.showAssetPreview(terrain.id)}
    onTerrainChanged:refresh()
    onVisibleChanged:refresh()
    Component.onCompleted:refresh()
   }
   ScrollView {id:terrainControls;SplitView.preferredWidth:320;clip:true;contentWidth:availableWidth;TerrainPanel {width:terrainControls.availableWidth;enabled:editor.selectedNode===panel.asset.id&&!editor.assetBusy}}
  }
 }
 Component {
  id:roadPage
   ColumnLayout {
    RowLayout {Layout.fillWidth:true;Layout.margins:12
     WorkbenchButton {text:"在地表添加节点";onClicked:{editor.beginPlacement("@road");panel.placeRequested()}}
     Label{text:"按 Esc 结束放置；指定两个节点生成连接。";color:Theme.secondary;Layout.fillWidth:true}
    }
    RowLayout {Layout.fillWidth:true;Layout.margins:12
     WorkbenchComboBox {id:from;model:editor.roadNodes;textRole:"name";valueRole:"id";Layout.fillWidth:true}
     Label{text:"→"}
     WorkbenchComboBox {id:to;model:editor.roadNodes;textRole:"name";valueRole:"id";Layout.fillWidth:true}
     WorkbenchSpinBox{id:roadWidth;from:1;to:64;value:5}
     WorkbenchButton {text:"生成道路";enabled:from.currentValue!==to.currentValue&&!!to.currentValue;onClicked:editor.connectRoadNodes(from.currentValue,to.currentValue,roadWidth.value)}
    }
    ListView {
     Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:editor.roadEdges
     delegate:Frame {
      required property var modelData;width:ListView.view.width;height:110
      function patch(k,v){let c={};c[k]=v;editor.creationEdit("patch",{kind:"roadEdges",id:modelData.id,changes:c})}
      ColumnLayout {anchors.fill:parent
       RowLayout {Layout.fillWidth:true
        Label{text:modelData.from.slice(-6)+" → "+modelData.to.slice(-6);Layout.preferredWidth:140;color:Theme.secondary}
        WorkbenchComboBox {model:["贴地切填","桥梁","隧道"];currentIndex:modelData.bridge?1:modelData.tunnel?2:0;onActivated:editor.creationEdit("patch",{kind:"roadEdges",id:modelData.id,changes:{bridge:currentIndex===1,tunnel:currentIndex===2}})}
        Label{text:"宽度"}WorkbenchSpinBox{from:1;to:64;value:modelData.width;onValueModified:patch("width",value)}
        Item{Layout.fillWidth:true}
        WorkbenchButton {text:"删除";onClicked:editor.creationEdit("remove",{kind:"roadEdges",id:modelData.id})}
       }
       RowLayout {Layout.fillWidth:true;Label{text:"最大坡度°"}WorkbenchSpinBox{from:1;to:80;value:modelData.maxSlope;onValueModified:patch("maxSlope",value)}Label{text:"隧道净高"}WorkbenchSpinBox{from:2;to:32;value:modelData.clearance;onValueModified:patch("clearance",value)}Label{text:"路面方块"}WorkbenchTextField {Layout.fillWidth:true;text:modelData.surface;onEditingFinished:patch("surface",text)}WorkbenchCheckBox {text:"启用";checked:modelData.enabled;onToggled:patch("enabled",checked)}}
      }
     }
     ScrollBar.vertical:ScrollBar{}
    }
    Label{Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;text:"贴地道路在地表采样并按节点高程调整坡度；桥梁保持节点高程，隧道按可调净高挖空路面上方。路口由连接覆盖区域合并。节点高程和位置可在大纲详情修改。";color:Theme.secondary}
   }
 }
}
