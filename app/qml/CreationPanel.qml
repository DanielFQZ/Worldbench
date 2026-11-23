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
 property int page:0
 function editingText(){let w=panel.Window.window,item=w?w.activeFocusItem:null;return item&&(typeof item.selectAll==="function"||typeof item.inputMethodComposing==="boolean")}
 Shortcut{sequence:editor.shortcut("undo");enabled:panel.visible&&!panel.editingText()&&!editor.commandBusy;onActivated:editor.undo()}
 Shortcut{sequences:[editor.shortcut("redo"),"Ctrl+Shift+Z"];enabled:panel.visible&&!panel.editingText()&&!editor.commandBusy;onActivated:editor.redo()}
 background:Rectangle{color:"#202630"}
 ColumnLayout {
  anchors.fill:parent;spacing:0
  TabBar {id:tabs;objectName:"creationTabs";Layout.fillWidth:true;onCurrentIndexChanged:panel.page=currentIndex
   TabButton{text:"PCG 节点图"}TabButton{text:"资产库 / 编辑"}TabButton{text:"道路网络"}
  }
  Label{Layout.fillWidth:true;Layout.margins:8;visible:(editor.creationState.diagnostics||[]).length>0;text:(editor.creationState.diagnostics||[]).join("\n");color:"#edbe82";wrapMode:Text.WordWrap}
  StackLayout {
   Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:panel.page
   ColumnLayout {
    RowLayout {Layout.fillWidth:true;Layout.margins:8
     ComboBox {id:graphs;Layout.preferredWidth:250;model:editor.pcgGraphs;textRole:"name";valueRole:"id";onActivated:panel.graphId=currentValue;onCurrentValueChanged:if(!panel.graphId)panel.graphId=currentValue||""}
     Button {text:"+ 图";onClicked:{if(editor.createPcgGraph()){panel.graphId=editor.pcgGraphs[editor.pcgGraphs.length-1].id;graphs.currentIndex=editor.pcgGraphs.length-1}}}
     Button {text:"+ 立方体范围";enabled:!!panel.graphId;onClicked:editor.addPcgVolume(panel.graphId)}
     Label {text:"范围和过渡在场景大纲中编辑";Layout.fillWidth:true;color:"#90a5ba"}
    }
    TabBar {Layout.fillWidth:true;currentIndex:editor.pcgGraphs.findIndex(g=>g.id===panel.graphId);Repeater{model:editor.pcgGraphs;TabButton{required property var modelData;text:modelData.name;onClicked:panel.graphId=modelData.id}}}
    PcgGraphEditor {Layout.fillWidth:true;Layout.fillHeight:true;graphId:panel.graphId}
   }
   SplitView {
    ColumnLayout {
     SplitView.preferredWidth:240;SplitView.minimumWidth:190
     RowLayout {Layout.fillWidth:true;Layout.margins:8
      Button{text:"导入…";onClicked:{assetFile.currentFolder=editor.dialogFolder("structureImport");assetFile.open()}}
      Button{text:"保存预设…";enabled:!!panel.assetId;onClicked:{presetFile.currentFolder=editor.dialogFolder("assetPresetSave");presetFile.open()}}
     }
     TextField{id:assetSearch;Layout.fillWidth:true;Layout.margins:8;placeholderText:"搜索资产…"}
     ListView {
      Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:editor.structureAssets.filter(a=>!assetSearch.text||a.name.indexOf(assetSearch.text)>=0)
      delegate:Rectangle {
       id:tile;required property var modelData;width:ListView.view.width;height:74;color:panel.assetId===modelData.id?"#365365":"#29333f";border.color:"#202630"
       Text {x:12;y:10;text:tile.modelData.name;color:"#d7e5f3";width:parent.width-24;elide:Text.ElideRight}
       Text {x:12;y:36;text:tile.modelData.width+" × "+tile.modelData.height+" × "+tile.modelData.depth+"  ·  "+tile.modelData.blockCount+" 块";color:"#9bb0c5";font.pixelSize:11}
       MouseArea {anchors.fill:parent;onClicked:panel.assetId=tile.modelData.id;onDoubleClicked:{editor.beginPlacement(tile.modelData.id);panel.placeRequested()}}
      }
      ScrollBar.vertical:ScrollBar{}
     }
     Label {text:"双击资产后在地表点击放置；\n主视口 ▣ 快捷库支持拖动放置。";Layout.margins:10;color:"#92a8bc"}
    }
    ColumnLayout {SplitView.fillWidth:true
     TabBar {Layout.fillWidth:true;currentIndex:editor.structureAssets.findIndex(a=>a.id===panel.assetId);Repeater{model:editor.structureAssets;TabButton{required property var modelData;text:modelData.name;onClicked:panel.assetId=modelData.id}}}
     AssetEditor {Layout.fillWidth:true;Layout.fillHeight:true;assetId:panel.assetId;onPlaceRequested:panel.placeRequested()}
    }
   }
   ColumnLayout {
    RowLayout {Layout.fillWidth:true;Layout.margins:12
     Button{text:"在地表添加节点";onClicked:{editor.beginPlacement("@road");panel.placeRequested()}}
     Label{text:"按 Esc 结束放置；指定两个节点生成连接。";color:"#9cb1c4";Layout.fillWidth:true}
    }
    RowLayout {Layout.fillWidth:true;Layout.margins:12
     ComboBox{id:from;model:editor.roadNodes;textRole:"name";valueRole:"id";Layout.fillWidth:true}
     Label{text:"→"}
     ComboBox{id:to;model:editor.roadNodes;textRole:"name";valueRole:"id";Layout.fillWidth:true}
     WorkbenchSpinBox{id:roadWidth;from:1;to:64;value:5}
     Button{text:"生成道路";enabled:from.currentValue!==to.currentValue&&!!to.currentValue;onClicked:editor.connectRoadNodes(from.currentValue,to.currentValue,roadWidth.value)}
    }
    ListView {
     Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:editor.roadEdges
     delegate:Frame {
      required property var modelData;width:ListView.view.width;height:110
      function patch(k,v){let c={};c[k]=v;editor.creationEdit("patch",{kind:"roadEdges",id:modelData.id,changes:c})}
      ColumnLayout {anchors.fill:parent
       RowLayout {Layout.fillWidth:true
        Label{text:modelData.from.slice(-6)+" → "+modelData.to.slice(-6);Layout.preferredWidth:140;color:"#a6c5dc"}
        ComboBox{model:["贴地切填","桥梁","隧道"];currentIndex:modelData.bridge?1:modelData.tunnel?2:0;onActivated:editor.creationEdit("patch",{kind:"roadEdges",id:modelData.id,changes:{bridge:currentIndex===1,tunnel:currentIndex===2}})}
        Label{text:"宽度"}WorkbenchSpinBox{from:1;to:64;value:modelData.width;onValueModified:patch("width",value)}
        Item{Layout.fillWidth:true}
        Button{text:"删除";onClicked:editor.creationEdit("remove",{kind:"roadEdges",id:modelData.id})}
       }
       RowLayout {Layout.fillWidth:true;Label{text:"最大坡度°"}WorkbenchSpinBox{from:1;to:80;value:modelData.maxSlope;onValueModified:patch("maxSlope",value)}Label{text:"隧道净高"}WorkbenchSpinBox{from:2;to:32;value:modelData.clearance;onValueModified:patch("clearance",value)}Label{text:"路面方块"}TextField{Layout.fillWidth:true;text:modelData.surface;onEditingFinished:patch("surface",text)}CheckBox{text:"启用";checked:modelData.enabled;onToggled:patch("enabled",checked)}}
      }
     }
     ScrollBar.vertical:ScrollBar{}
    }
    Label{Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;text:"贴地道路在地表采样并按节点高程调整坡度；桥梁保持节点高程，隧道按可调净高挖空路面上方。路口由连接覆盖区域合并。节点高程和位置可在大纲详情修改。";color:"#94aabe"}
   }
  }
 }
 FileDialog{id:assetFile;title:"导入结构资产";nameFilters:["结构资产 (*.schem *.schematic *.nbt *.json *.wbasset)"];onRejected:editor.rememberDialogFolder("structureImport",currentFolder,true);onAccepted:{editor.rememberDialogFolder("structureImport",selectedFile);if(editor.importStructureAsset(selectedFile))panel.assetId=editor.structureAssets[editor.structureAssets.length-1].id}}
 FileDialog{id:presetFile;title:"保存便携资产预设";fileMode:FileDialog.SaveFile;nameFilters:["Worldbench 资产 (*.wbasset)"];onRejected:editor.rememberDialogFolder("assetPresetSave",currentFolder,true);onAccepted:{editor.rememberDialogFolder("assetPresetSave",selectedFile);editor.saveAssetPreset(panel.assetId,selectedFile)}}
}
