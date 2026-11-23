import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
 id: root
 objectName:"pcgGraphEditor"
 property string graphId: ""
 property var graph: ({nodes:[],connections:[]})
 property string selected: ""
 property string wiring: ""
 property string message: ""
 property string signature:""
 function refresh(){if(editor.creationBusy)return;let value=editor.graphData(graphId),sig=JSON.stringify(value);if(sig===signature)return;signature=sig;graph=value.nodes?value:{nodes:[],connections:[]};wires.requestPaint()}

 onGraphIdChanged:refresh()
 Connections {target:editor;function onChanged(){root.refresh()}}
 function clone(){return JSON.parse(JSON.stringify(graph))}
 function spec(type){for(const s of editor.pcgNodeTypes)if(s.type===type)return s;return {name:type,inputs:[],parameters:{}}}
 function save(g){if(editor.creationEdit("graph",g))message="已更新 · Ctrl+Z 撤销";else message="未提交：请检查错误提示"}
 function addNode(){if(!graph.id||!nodeType.currentValue)return;let g=clone();g.nodes.push({id:"node-"+Date.now(),type:nodeType.currentValue,parameters:Object.assign({},spec(nodeType.currentValue).parameters,{uiX:80+g.nodes.length*20,uiY:150+g.nodes.length*25})});save(g)}
 function remove(){let g=clone();g.nodes=g.nodes.filter(n=>n.id!==selected);g.connections=g.connections.filter(c=>c.fromNode!==selected&&c.toNode!==selected);save(g);selected=""}
 function patchParam(key,value){let g=clone();for(let n of g.nodes)if(n.id===selected)n.parameters[key]=value;save(g)}
 function paramName(k){return ({name:"暴露参数名",default:"默认值",graph:"内嵌字段子图",value:"输入值",threshold:"阈值",block:"表层方块",under:"下层方块",depth:"材料深度（格）",alternatives:"加权材料 [{block,weight}]",assets:"加权资产 [{id,weight}]",assetId:"散布资产",density:"密度 0–1",spacing:"采样格距（最小间距为一半）",rotation:"随机朝向",minScale:"最小整数缩放",maxScale:"最大整数缩放",embed:"根部埋入深度",scale:"噪声尺度（格）",octaves:"噪声层数",amplitude:"幅度",offset:"偏移",min:"下限",max:"上限",feather:"过渡宽度",inMin:"输入下限",inMax:"输入上限",outMin:"输出下限",outMax:"输出上限",exponent:"曲线指数",radius:"蒙版笔刷半径",points:"蒙版点 [[x,z]]"})[k]||k}
 function node(id){for(let n of graph.nodes||[])if(n.id===id)return n;return null}
 function position(n){return {x:n.parameters.uiX===undefined?80:n.parameters.uiX,y:n.parameters.uiY===undefined?80:n.parameters.uiY}}
 ColumnLayout {
  anchors.fill:parent;spacing:0
  RowLayout {
   Layout.fillWidth:true;Layout.margins:8
   TextField {id:search;placeholderText:"搜索节点…";Layout.preferredWidth:120}
   ComboBox {id:nodeType;Layout.preferredWidth:180;model:editor.pcgNodeTypes.filter(n=>!search.text||n.name.indexOf(search.text)>=0||n.type.indexOf(search.text)>=0);textRole:"name";valueRole:"type"}
   Button {text:"添加节点";enabled:!!root.graph.id;onClicked:root.addNode()}
   Button {text:"删除节点";enabled:!!root.selected;onClicked:root.remove()}
   Item {Layout.fillWidth:true}
   Button {text:"导入图…";onClicked:{graphFile.fileMode=FileDialog.OpenFile;graphFile.currentFolder=editor.dialogFolder("pcgGraphImport");graphFile.open()}}
   Button {text:"保存图…";enabled:!!root.graph.id;onClicked:{graphFile.fileMode=FileDialog.SaveFile;graphFile.currentFolder=editor.dialogFolder("pcgGraphSave");graphFile.open()}}
  }
  SplitView {
   Layout.fillWidth:true;Layout.fillHeight:true
   Rectangle {
    SplitView.fillWidth:true;color:"#171d26"
    Flickable {
     id:canvasView;objectName:"pcgCanvas";anchors.fill:parent;clip:true;contentWidth:2400;contentHeight:1600;boundsBehavior:Flickable.StopAtBounds
     ScrollBar.horizontal:ScrollBar{}
     ScrollBar.vertical:ScrollBar{}
     Canvas {
      id:wires;width:2400;height:1600
      onPaint:{let c=getContext("2d");c.reset();c.strokeStyle="#232c38";c.lineWidth=1;for(let x=0;x<width;x+=24){c.beginPath();c.moveTo(x,0);c.lineTo(x,height);c.stroke()}for(let y=0;y<height;y+=24){c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke()}
       for(const link of root.graph.connections||[]){const a=root.node(link.fromNode),b=root.node(link.toNode);if(!a||!b)continue;const pa=root.position(a),pb=root.position(b);const port=root.spec(b.type).inputs.indexOf(link.toPort);let x1=pa.x+198,y1=pa.y+52,x2=pb.x+2,y2=pb.y+80+port*24;c.strokeStyle="#82c9b8";c.lineWidth=2;c.beginPath();c.moveTo(x1,y1);c.bezierCurveTo(x1+90,y1,x2-90,y2,x2,y2);c.stroke()}
      }
     }
     Repeater {
      model:root.graph.nodes||[]
      delegate:Rectangle {
       id:card;required property var modelData;required property int index
       x:root.position(modelData).x;y:root.position(modelData).y;width:200;height:Math.max(112,92+root.spec(modelData.type).inputs.length*24);radius:7
       color:"#26313e";border.width:root.selected===modelData.id?2:1;border.color:root.selected===modelData.id?"#76d9c5":"#455467"
       Rectangle {width:parent.width;height:34;radius:7;color:modelData.type.indexOf("output_")===0?"#365747":"#334a61"
        Text {anchors.centerIn:parent;text:root.spec(card.modelData.type).name;color:"#e7eef6";font.bold:true}
        MouseArea {anchors.fill:parent;drag.target:card;drag.minimumX:0;drag.minimumY:0
         onPressed:{root.forceActiveFocus();root.selected=card.modelData.id}
         onReleased:{let g=root.clone();let n=g.nodes[card.index];n.parameters.uiX=card.x;n.parameters.uiY=card.y;root.save(g)}
        }
       }
       Text{text:"value  ●";color:root.wiring===modelData.id?"#ffee9b":"#82c9b8";anchors.right:parent.right;anchors.rightMargin:8;y:44
        MouseArea{anchors.fill:parent;anchors.margins:-5;onClicked:{root.forceActiveFocus();root.wiring=card.modelData.id;root.message="选择目标节点的输入端口；右键输入端口断开"}}
       }
       Repeater {model:root.spec(card.modelData.type).inputs
        delegate:Text {required property string modelData;required property int index;x:5;y:72+index*24;text:"●  "+modelData;color:"#b8c9db"
         MouseArea {anchors.fill:parent;anchors.margins:-5;acceptedButtons:Qt.LeftButton|Qt.RightButton
          onClicked:function(mouse){root.forceActiveFocus();if(mouse.button===Qt.RightButton){let g=root.clone();g.connections=g.connections.filter(c=>!(c.toNode===card.modelData.id&&c.toPort===modelData));root.save(g)}else if(root.wiring){editor.connectPcgNodes(root.graphId,root.wiring,"value",card.modelData.id,modelData);root.wiring=""}}
         }
        }
       }
      }
     }
    }
    Label {anchors.centerIn:parent;visible:!root.graph.id;text:"创建或选择一个 PCG 图";color:"#8091a4"}
   }
   ScrollView {
    id:nodeDetailsScroll
    SplitView.preferredWidth:285;SplitView.minimumWidth:240;clip:true;contentWidth:availableWidth
    ColumnLayout {
     width:nodeDetailsScroll.availableWidth;spacing:8
     property var current:root.node(root.selected)
     Label {text:parent.current?root.spec(parent.current.type).name:"节点参数";font.bold:true;Layout.margins:12}
     Label {text:"点击输出圆点，再点输入圆点连接。右键输入断开。节点标题可拖动。";wrapMode:Text.WordWrap;Layout.fillWidth:true;Layout.margins:12;color:"#8f9fb2"}
     Image {
      id:fieldPreview;Layout.fillWidth:true;Layout.preferredHeight:180;fillMode:Image.PreserveAspectFit;cache:false
      source:{root.signature;return root.selected?editor.pcgPreview(root.graphId,root.selected):""}

      MouseArea {anchors.fill:parent;enabled:!!root.node(root.selected)&&root.node(root.selected).type==="paint_mask"
       function paint(m){let vols=editor.pcgVolumes.filter(v=>v.graphId===root.graphId),v=vols.length?vols[0]:{min:[-32,0,-32],max:[32,320,32]};let size=Math.min(width,height),left=(width-size)/2,top=(height-size)/2;if(m.x<left||m.x>left+size||m.y<top||m.y>top+size)return;let pts=(root.node(root.selected).parameters.points||[]).slice();pts.push([v.min[0]+(m.x-left)/size*(v.max[0]-v.min[0]),v.min[2]+(m.y-top)/size*(v.max[2]-v.min[2])]);root.patchParam("points",pts)}
       onClicked:function(m){paint(m)}
      }
     }
     Label{Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#90a5ba";text:"字段预览：黑0 / 白1。手绘蒙版节点可点击图像绘制圆盘；范围取当前图的第一个实例。"}
     ComboBox {visible:!!root.node(root.selected)&&root.node(root.selected).type==="subgraph";Layout.fillWidth:true;Layout.margins:12;model:editor.pcgGraphs.filter(g=>g.id!==root.graphId);textRole:"name";valueRole:"id";onActivated:root.patchParam("graph",editor.graphData(currentValue))}
     Repeater {
      model:{const n=root.node(root.selected);return n?Object.keys(Object.assign({},root.spec(n.type).parameters,n.parameters)).filter(k=>k!=="uiX"&&k!=="uiY"&&k!=="graph"&&k!=="points"):[]}
      delegate:ColumnLayout {
       required property string modelData
       Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
       property var value:{const n=root.node(root.selected);return n?Object.assign({},root.spec(n.type).parameters,n.parameters)[modelData]:""}
       Label {text:root.paramName(modelData);color:"#b8c7d7"}
       TextField {visible:typeof parent.value!=="boolean"&&typeof parent.value!=="number";Layout.fillWidth:true;text:typeof parent.value==="object"?JSON.stringify(parent.value):String(parent.value)
        onEditingFinished:{const old=parent.value;try{let value=typeof old==="number"?Number(text):typeof old==="boolean"?text==="true":typeof old==="object"?JSON.parse(text):text;if(typeof value==="number"&&!isFinite(value))return;root.patchParam(parent.modelData,value)}catch(e){root.message="参数格式错误："+e}}
       }
       WorkbenchSpinBox {visible:typeof parent.value==="number";Layout.fillWidth:true;from:-100000000;to:100000000;property real multiplier:1000;value:Number(parent.value)*multiplier;textFromValue:function(v){return String(v/multiplier)};valueFromText:function(t){return Math.round(Number(t)*multiplier)};stepSize:100;onValueModified:root.patchParam(parent.modelData,value/multiplier)}
       CheckBox {visible:typeof parent.value==="boolean";checked:parent.value===true;text:"启用";onToggled:root.patchParam(parent.modelData,checked)}
       ComboBox {visible:modelData==="block"||modelData==="under";Layout.fillWidth:true;model:editor.blockCatalog("");textRole:"title";valueRole:"name";onActivated:root.patchParam(parent.modelData,currentValue)}
       ComboBox {visible:modelData==="assetId";Layout.fillWidth:true;model:editor.structureAssets;textRole:"name";valueRole:"id";onActivated:root.patchParam("assetId",currentValue)}
      }
     }
     Label {text:"数量："+editor.creationState.scatterCount+" 个散布实例\n生成："+editor.creationState.generatedCells+" 个方块";Layout.margins:12;color:"#83c8b8"}
    }
   }
  }
  Label {Layout.fillWidth:true;Layout.margins:8;text:root.message||"参数与连接修改自动生成；范围、种子和过渡在场景大纲详情中调整。";color:"#9db0c4";elide:Text.ElideRight}
 }
 FileDialog{id:graphFile;title:fileMode===FileDialog.SaveFile?"保存 PCG 图":"导入 PCG 图";nameFilters:["Worldbench PCG (*.wbpcg *.json)"];onRejected:editor.rememberDialogFolder(fileMode===FileDialog.SaveFile?"pcgGraphSave":"pcgGraphImport",currentFolder,true);onAccepted:{editor.rememberDialogFolder(fileMode===FileDialog.SaveFile?"pcgGraphSave":"pcgGraphImport",selectedFile);editor.creationFile(fileMode===FileDialog.SaveFile?"save_graph":"load_graph",root.graphId,selectedFile)}}
}

