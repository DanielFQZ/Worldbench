import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Worldbench 1.0
Item {
 id:root;objectName:"assetEditor"
 property var controller:editor
 property string assetId:""
 property var asset:({})
 property var analysis:({})
 property var sliceData:({blocks:[],labels:[]})
 function refreshSlice(){sliceData=editor.assetSlice(assetId,sliceY.value,showAnalysis.checked);slice.requestPaint()}
 property var markStart:[0,0,0]
 property var markEnd:[0,0,0]
 signal placeRequested()
 property string signature:""
 function refresh(){if(editor.creationBusy)return;let a=editor.assetData(assetId),sig=JSON.stringify(a)+JSON.stringify(editor.caveMarkers);if(sig===signature)return;signature=sig;asset=a;editor.showAssetPreview(assetId);refreshSlice()}
 onAssetIdChanged:{refresh();analysis={}}
 Connections{target:editor;function onChanged(){root.refresh()}}
 function patch(key,value){let c={};c[key]=value;editor.creationEdit("patch",{kind:"assets",id:assetId,changes:c})}
 ColumnLayout {
  anchors.fill:parent;spacing:8
  RowLayout {Layout.fillWidth:true;Layout.margins:8
   TextField {Layout.fillWidth:true;text:root.asset.name||"";placeholderText:"选择资产";onEditingFinished:root.patch("name",text)}
   Button {text:"在地表放置";enabled:!!root.assetId;onClicked:{editor.beginPlacement(root.assetId);root.placeRequested()}}
  }
  SplitView {
   Layout.fillWidth:true;Layout.fillHeight:true
   ColumnLayout {
    SplitView.fillWidth:true
    Rectangle {
     Layout.fillWidth:true;Layout.fillHeight:true;color:"#151e29";clip:true
     VoxelViewport {id:preview;objectName:"assetPreviewViewport";anchors.fill:parent;editor:root.controller.assetInspector}
     Button {
      x:12;y:12;width:36;height:36;text:"▣"
      enabled:!!root.assetId
      onClicked:{editor.beginPlacement(root.assetId);root.placeRequested()}
      ToolTip.visible:hovered;ToolTip.text:"在地表放置；主视口资产快捷库支持拖放"
     }
     Label{anchors.bottom:parent.bottom;anchors.left:parent.left;anchors.margins:8;text:"真实方块 / 模型预览 · 右键转向，Alt+左键环绕；下方剖面精确标记";color:"#8fa8bd"}
    }
    RowLayout {Layout.fillWidth:true;Label{text:"剖面 Y"}WorkbenchSpinBox{id:sliceY;objectName:"assetSliceY";from:0;to:Math.max(0,(root.asset.height||1)-1);value:0;onValueChanged:root.refreshSlice()}Label{text:"点击设起点，Shift+点击设终点";color:"#90a5ba"}}
    Canvas {
     id:slice;Layout.fillWidth:true;Layout.preferredHeight:140
     onPaint:{let c=getContext("2d");c.reset();c.fillStyle="#121b25";c.fillRect(0,0,width,height);if(!root.asset.width)return;let size=Math.min(width/root.asset.width,height/root.asset.depth);for(let b of root.sliceData.blocks||[]){c.fillStyle="#8497a6";c.fillRect(b[0]*size,b[1]*size,Math.max(1,size),Math.max(1,size))}for(let b of root.sliceData.labels||[]){c.fillStyle=b[2]===1?"#4ab5c9":b[2]===2?"#eec365":b[2]===4?"#b78be3":"#f36c6c";c.fillRect(b[0]*size,b[1]*size,Math.max(1,size),Math.max(1,size))}c.strokeStyle="#ffbc68";c.lineWidth=2;let a=root.markStart,b=root.markEnd;c.strokeRect(Math.min(a[0],b[0])*size,Math.min(a[2],b[2])*size,(Math.abs(a[0]-b[0])+1)*size,(Math.abs(a[2]-b[2])+1)*size)}
     MouseArea {anchors.fill:parent;onClicked:function(m){let size=Math.min(slice.width/root.asset.width,slice.height/root.asset.depth);let p=[Math.floor(m.x/size),sliceY.value,Math.floor(m.y/size)];if(p[0]>=root.asset.width||p[2]>=root.asset.depth)return;if(m.modifiers&Qt.ShiftModifier)root.markEnd=p;else root.markStart=root.markEnd=p;slice.requestPaint()}}
    }
   }
   ScrollView {
    id:assetDetailsScroll
    SplitView.preferredWidth:300;SplitView.minimumWidth:260;clip:true;contentWidth:availableWidth
    ColumnLayout {
     width:assetDetailsScroll.availableWidth;spacing:8
     Label{text:"资产写入 / 地基";font.bold:true;Layout.margins:8}
     ComboBox {Layout.fillWidth:true;model:[{id:"place",name:"Place · 仅放实体块"},{id:"carve",name:"Carve · 实体形状挖空"},{id:"ignore",name:"Ignore · 保留目标"},{id:"cave",name:"洞穴 · 壳体 + 内部挖空"}];textRole:"name";valueRole:"id";currentIndex:Math.max(0,model.findIndex(x=>x.id===root.asset.placement));onActivated:root.patch("placement",currentValue)}
     ComboBox {Layout.fillWidth:true;model:["刚性放置","切填地形","支撑地基（2格）"];currentIndex:root.asset.groundBlend||0;onActivated:root.patch("groundBlend",currentIndex)}
     Label{text:"根 / 底座枢轴 XYZ";color:"#b6c9db"}
     RowLayout {Layout.fillWidth:true;Repeater{model:3;delegate:WorkbenchSpinBox{required property int index;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1;compact:true;from:-512;to:512;value:(root.asset.pivot||[0,0,0])[index];onValueModified:{let v=root.asset.pivot.slice();v[index]=value;root.patch("pivot",v)}}}}
     Button{text:"将剖面起点设为枢轴";onClicked:root.patch("pivot",root.markStart)}
     Label{text:"洞穴标记";font.bold:true;Layout.topMargin:12}
     ComboBox{id:markerKind;Layout.fillWidth:true;model:[{id:"portal",name:"入口虚拟封口面"},{id:"exit",name:"出口虚拟封口面"},{id:"seal",name:"辅助封口面"},{id:"interior",name:"内部种子"}];textRole:"name";valueRole:"id"}
     Repeater {model:2;delegate:ColumnLayout{required property int index;property int endpoint:index
      Label{text:parent.endpoint===0?"起点 XYZ":"终点 XYZ（面/盒范围）";color:"#9aadc1"}
      RowLayout{Layout.fillWidth:true;Repeater{model:3;delegate:WorkbenchSpinBox{required property int index;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1;compact:true;from:0;to:511;value:(endpoint===0?root.markStart:root.markEnd)[index];onValueModified:{let p=(endpoint===0?root.markStart:root.markEnd).slice();p[index]=value;if(endpoint===0)root.markStart=p;else root.markEnd=p;slice.requestPaint()}}}}
     }}
     Button{text:"添加标记";onClicked:editor.creationEdit("add",{kind:"caveMarkers",changes:{assetId:root.assetId,kind:markerKind.currentValue,position:root.markStart,end:root.markEnd,enabled:true}})}
     Repeater {model:editor.caveMarkers.filter(m=>m.assetId===root.assetId);delegate:RowLayout{required property var modelData;Layout.fillWidth:true;Label{Layout.fillWidth:true;text:modelData.kind+" "+modelData.position.join(",");elide:Text.ElideRight}Button{text:"×";onClicked:editor.creationEdit("remove",{kind:"caveMarkers",id:modelData.id})}}}
     CheckBox{id:showAnalysis;objectName:"assetShowAnalysis";Layout.fillWidth:true;text:"剖面分类：蓝内部 / 黄入口\n红漏口 / 紫待确认";onToggled:root.refreshSlice()}
     Button{text:"分析内外 / 检查泄漏";onClicked:{root.analysis=editor.caveAnalysis(root.assetId);showAnalysis.checked=true;root.refreshSlice()}}
     Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:root.analysis.valid?"#8bd7b9":"#e5ba83";text:root.analysis.valid?"内部 "+root.analysis.interior+" 格；入口/出口 "+root.analysis.portals+" 格。可切换到洞穴模式试放。":(root.analysis.diagnostics||[]).join("\n")}
     Label{Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#d5ac78";text:root.asset.diagnostic||""}
    }
   }
  }
 }
}
