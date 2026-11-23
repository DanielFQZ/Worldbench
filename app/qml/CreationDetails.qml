import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
 id:root;objectName:"creationDetailsPanel"
 property var detailsData:editor.creationDetails
 spacing:9
 function patch(key,value){let changes={};changes[key]=value;editor.creationEdit("patch",{kind:detailsData.kind,id:detailsData.id,changes:changes})}
 Label {text:root.detailsData.kind==="volumes"?"PCG 范围":root.detailsData.kind==="presets"?"结构实例":"道路对象";font.bold:true;Layout.margins:12}
 TextField {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;text:root.detailsData.name||"";onEditingFinished:root.patch("name",text)}
 ComboBox {visible:root.detailsData.kind==="volumes";Layout.fillWidth:true;Layout.margins:12;model:editor.pcgGraphs;textRole:"name";valueRole:"id";currentIndex:model.findIndex(g=>g.id===root.detailsData.graphId);onActivated:root.patch("graphId",currentValue)}
 Repeater {
  model:root.detailsData.kind==="volumes"?[{key:"min",name:"范围下界"},{key:"max",name:"范围上界"}]:root.detailsData.kind==="presets"?[{key:"position",name:"位移"},{key:"rotation",name:"旋转（Y 轴90°步进）"},{key:"scale",name:"整数缩放 / 负值镜像"}]:root.detailsData.kind==="roadNodes"?[{key:"position",name:"节点位置"}]:[]
  delegate:ColumnLayout {
   required property var modelData
   id:triplet;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:triplet.modelData.name;color:"#9eb1c7"}
   RowLayout {
    Layout.fillWidth:true
    Repeater {model:3
     delegate:ColumnLayout {required property int index;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1
      Label{text:["X","Y","Z"][index];color:["#e88b8b","#9bd497","#8ebaff"][index]}
      WorkbenchSpinBox {Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1;compact:true;from:-100000;to:100000;value:(root.detailsData[triplet.modelData.key]||[0,0,0])[index];stepSize:triplet.modelData.key==="rotation"?90:1
       onValueModified:{let v=(root.detailsData[triplet.modelData.key]||[0,0,0]).slice();v[index]=value;root.patch(triplet.modelData.key,v)}
      }
     }
    }
   }
  }
 }
 ColumnLayout {
  visible:root.detailsData.kind==="volumes";Layout.fillWidth:true;Layout.margins:12
  Label{text:"过渡宽度（格）"}
  WorkbenchSpinBox {from:0;to:4096;value:root.detailsData.transition||0;onValueModified:root.patch("transition",value)}
  Label{text:"实例参数覆盖";font.bold:true}
  Repeater {model:{let g=editor.graphData(root.detailsData.graphId||"");return (g.nodes||[]).filter(n=>n.type==="parameter")}
   delegate:RowLayout {required property var modelData;Layout.fillWidth:true;Label{text:modelData.parameters.name||"参数";Layout.fillWidth:true}
    WorkbenchSpinBox{from:-100000000;to:100000000;value:Math.round(((root.detailsData.overrides||{})[modelData.parameters.name]===undefined?modelData.parameters.default:(root.detailsData.overrides||{})[modelData.parameters.name])*1000);textFromValue:function(v){return String(v/1000)};valueFromText:function(t){return Math.round(Number(t)*1000)};stepSize:100;onValueModified:{let p=Object.assign({},root.detailsData.overrides||{});p[modelData.parameters.name]=value/1000;root.patch("overrides",p)}}
   }
  }
  Label{text:"随机种子"}
  WorkbenchSpinBox {from:0;to:1000000;value:root.detailsData.seed||1;onValueModified:root.patch("seed",value)}
  Label{text:"绑定地貌实例（空为独立范围）";color:"#a3b6c9"}
  ComboBox {Layout.fillWidth:true;model:[{id:"",name:"独立范围"}].concat(editor.sceneNodes.filter(n=>n.kind==="heightmap"));textRole:"name";valueRole:"id";currentIndex:model.findIndex(n=>n.id===(root.detailsData.sourceId||""));onActivated:root.patch("sourceId",currentValue)}
  CheckBox {text:"仅实际贡献区域";checked:root.detailsData.contributionOnly!==false;onToggled:root.patch("contributionOnly",checked)}
 }
 Button {Layout.margins:12;text:"保存为资产预设…";visible:root.detailsData.kind==="presets";onClicked:{presetFile.currentFolder=editor.dialogFolder("assetPresetSave");presetFile.open()}}
 FileDialog{id:presetFile;fileMode:FileDialog.SaveFile;nameFilters:["Worldbench资产 (*.wbasset)"];onRejected:editor.rememberDialogFolder("assetPresetSave",currentFolder,true);onAccepted:{editor.rememberDialogFolder("assetPresetSave",selectedFile);editor.saveAssetPreset(root.detailsData.id,selectedFile)}}
 Button {Layout.margins:12;text:"删除此对象";onClicked:editor.creationEdit("remove",{kind:root.detailsData.kind,id:root.detailsData.id})}
}
