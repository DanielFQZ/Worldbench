import "Theme.js" as Theme
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
Pane {
 id:panel;objectName:"creationPanel";padding:0
 signal placeRequested()
 property string graphId:""
 property string assetId:""
 property var availableGraphs:editor.pcgGraphs
 property var availableAssets:editor.structureAssets
 onAvailableGraphsChanged:if(!availableGraphs.some(g=>g.id===graphId))graphId=availableGraphs.length?availableGraphs[0].id:""
 onAvailableAssetsChanged:if(!availableAssets.some(a=>a.id===assetId))assetId=availableAssets.length?availableAssets[0].id:""
 property bool embedded:false
 property alias page:tabs.currentIndex
 function editingText(){let w=panel.Window.window,item=w?w.activeFocusItem:null;return item&&(typeof item.selectAll==="function"||typeof item.inputMethodComposing==="boolean")}
 Shortcut{sequence:editor.shortcut("undo");enabled:!panel.embedded&&panel.visible&&!panel.editingText()&&!editor.commandBusy;onActivated:editor.undo()}
 Shortcut{sequences:[editor.shortcut("redo"),"Ctrl+Shift+Z"];enabled:!panel.embedded&&panel.visible&&!panel.editingText()&&!editor.commandBusy;onActivated:editor.redo()}
 background:Rectangle{color:Theme.panel}
 ColumnLayout {
  anchors.fill:parent;spacing:0
  TabBar {id:tabs;objectName:"creationTabs";Layout.fillWidth:true;visible:!panel.embedded
   WorkbenchTabButton {text:"PCG 节点图"}WorkbenchTabButton {text:"资产库 / 编辑"}WorkbenchTabButton {text:"道路网络"}
  }
  Label{Layout.fillWidth:true;Layout.margins:8;visible:(editor.creationState.diagnostics||[]).length>0;text:(editor.creationState.diagnostics||[]).join("\n");color:"#edbe82";wrapMode:Text.WordWrap}
  StackLayout {
   Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:panel.page
   ColumnLayout {
    RowLayout {Layout.fillWidth:true;Layout.margins:8
     WorkbenchComboBox {id:graphs;visible:false;Layout.preferredWidth:250;model:editor.pcgGraphs;textRole:"name";valueRole:"id";onActivated:panel.graphId=currentValue;onCurrentValueChanged:if(!panel.graphId)panel.graphId=currentValue||""}
     TabBar {Layout.fillWidth:true;clip:true;currentIndex:editor.pcgGraphs.findIndex(g=>g.id===panel.graphId);background:Rectangle{color:Theme.window} Repeater{model:editor.pcgGraphs;WorkbenchTabButton {required property var modelData;width:180;text:modelData.name;onClicked:panel.graphId=modelData.id}}}
     WorkbenchButton {text:"新建节点图";primary:true;onClicked:{if(editor.createPcgGraph()){panel.graphId=editor.pcgGraphs[editor.pcgGraphs.length-1].id;graphs.currentIndex=editor.pcgGraphs.length-1}}}
     WorkbenchButton {text:"创建范围实例";enabled:!!panel.graphId;onClicked:editor.addPcgVolume(panel.graphId)}
    }

    PcgGraphEditor {Layout.fillWidth:true;Layout.fillHeight:true;graphId:panel.graphId}
   }
   SplitView {
    ColumnLayout {
     SplitView.preferredWidth:240;SplitView.minimumWidth:190
     RowLayout {Layout.fillWidth:true;Layout.margins:8
      WorkbenchButton {text:"导入…";onClicked:{assetFile.currentFolder=editor.dialogFolder("structureImport");assetFile.open()}}
      WorkbenchButton {text:"保存预设…";enabled:!!panel.assetId;onClicked:{presetFile.currentFolder=editor.dialogFolder("assetPresetSave");presetFile.open()}}
     }
     WorkbenchTextField {id:assetSearch;Layout.fillWidth:true;Layout.margins:8;placeholderText:"搜索资产…"}
     ListView {
      Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:editor.structureAssets.filter(a=>!assetSearch.text||a.name.indexOf(assetSearch.text)>=0)
      delegate:Rectangle {
       id:tile;required property var modelData;width:ListView.view.width;height:74;color:panel.assetId===modelData.id?Theme.selection:Theme.panel;border.color:Theme.panel
       Text {x:12;y:10;text:tile.modelData.name;color:Theme.text;width:parent.width-24;elide:Text.ElideRight}
       Text {x:12;y:36;text:tile.modelData.width+" × "+tile.modelData.height+" × "+tile.modelData.depth+"  ·  "+tile.modelData.blockCount+" 块";color:Theme.secondary;font.pixelSize:11}
       MouseArea {anchors.fill:parent;onClicked:panel.assetId=tile.modelData.id;onDoubleClicked:{editor.beginPlacement(tile.modelData.id);panel.placeRequested()}}
      }
      ScrollBar.vertical:ScrollBar{}
     }
     Label {text:"双击资产后在地表点击放置；\n场景底部资产库支持拖动放置。";Layout.margins:10;color:Theme.secondary}
    }
    ColumnLayout {SplitView.fillWidth:true
     TabBar {Layout.fillWidth:true;clip:true;background:Rectangle{color:Theme.window} currentIndex:editor.structureAssets.findIndex(a=>a.id===panel.assetId);Repeater{model:editor.structureAssets;WorkbenchTabButton {required property var modelData;width:180;text:modelData.name;onClicked:panel.assetId=modelData.id}}}
     AssetEditor {Layout.fillWidth:true;Layout.fillHeight:true;assetId:panel.assetId;onPlaceRequested:panel.placeRequested()}
    }
   }
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
 FileDialog{id:assetFile;title:"导入结构资产";nameFilters:["结构资产 (*.schem *.schematic *.nbt *.json *.wbasset)"];onRejected:editor.rememberDialogFolder("structureImport",currentFolder,true);onAccepted:{editor.rememberDialogFolder("structureImport",selectedFile);if(editor.importStructureAsset(selectedFile))panel.assetId=editor.structureAssets[editor.structureAssets.length-1].id}}
 FileDialog{id:presetFile;title:"保存便携资产预设";fileMode:FileDialog.SaveFile;nameFilters:["Worldbench 资产 (*.wbasset)"];onRejected:editor.rememberDialogFolder("assetPresetSave",currentFolder,true);onAccepted:{editor.rememberDialogFolder("assetPresetSave",selectedFile);editor.saveAssetPreset(panel.assetId,selectedFile)}}
}
