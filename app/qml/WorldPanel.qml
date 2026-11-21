import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Pane {
 padding:0
 background:Rectangle { color:"#23262d" }
 palette.window:"#23262d"; palette.windowText:"#dce4ee"
 palette.base:"#191e26"; palette.text:"#dce4ee"
 palette.button:"#303741"; palette.buttonText:"#dce4ee"
 palette.highlight:"#344957"; palette.highlightedText:"#dce4ee"
 palette.placeholderText:"#7e8da1"
 property size kddockwidgets_min_size:Qt.size(280,400)
 SplitView {
  anchors.fill:parent;orientation:Qt.Vertical
  ColumnLayout {
   objectName:"sceneOutliner"
   SplitView.preferredHeight:220;SplitView.minimumHeight:120;spacing:2
   RowLayout{Layout.fillWidth:true;Layout.margins:8
    Label{text:"场景大纲";font.bold:true;color:"#c9d2df";Layout.fillWidth:true}
    Button{text:"+ 地形层";onClicked:editor.addSculptLayer();enabled:!editor.assetBusy}
   }
   ListView {
    id:sceneList;objectName:"sceneNodeList"
    Layout.fillWidth:true;Layout.fillHeight:true;clip:true
    ScrollBar.vertical:ScrollBar{}
    // Mutate stable rows by ID instead of replacing a QVariantList on every
    // editor notification (including mesh completion), which resets scrolling.
    model:ListModel{id:sceneModel}
    property var sourceNodes:editor.sceneNodes
    onSourceNodesChanged:syncNodes()
    Component.onCompleted:syncNodes()
    function syncNodes(){
     const nodes=sourceNodes||[]
     for(let i=0;i<nodes.length;++i){
      const n=nodes[i]
      let found=-1
      for(let j=i;j<sceneModel.count;++j)if(sceneModel.get(j).nodeId===n.id){found=j;break}
      const row={nodeId:String(n.id),nodeName:String(n.name),nodeKind:String(n.kind),nodeVisible:!!n.visible,nodeActive:!!n.active}
      if(found<0)sceneModel.insert(i,row)
      else{
       if(found!==i)sceneModel.move(found,i,1)
       for(const key in row)if(sceneModel.get(i)[key]!==row[key])sceneModel.setProperty(i,key,row[key])
      }
     }
     if(sceneModel.count>nodes.length)sceneModel.remove(nodes.length,sceneModel.count-nodes.length)
    }
    delegate:ItemDelegate{
     required property string nodeId
     required property string nodeName
     required property string nodeKind
     required property bool nodeVisible
     required property bool nodeActive
     id:node
     width:ListView.view.width;implicitHeight:34;text:(nodeActive?"✎  ":"▧  ")+nodeName;rightPadding:38;highlighted:editor.selectedNode===nodeId
     background:Rectangle { color:parent.highlighted?"#344957":parent.hovered?"#2d343e":"transparent" }
     onClicked:editor.selectedNode=nodeId
     Button {
      id:eyeButton
      objectName:"eye_"+node.nodeId
      anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:32;height:30
      flat:true;enabled:!editor.assetBusy
      property bool shown:node.nodeVisible
      onShownChanged:eyeDrawing.requestPaint()
      contentItem:Canvas {
       id:eyeDrawing
       onPaint:{var c=getContext("2d");c.reset();c.strokeStyle=eyeButton.shown?"#b5d8e8":"#6e7c8e";c.lineWidth=1.5;var x=width/2,y=height/2;c.beginPath();c.moveTo(x-9,y);c.quadraticCurveTo(x,y-10,x+9,y);c.quadraticCurveTo(x,y+10,x-9,y);c.stroke();c.beginPath();c.arc(x,y,3,0,Math.PI*2);c.stroke();if(!eyeButton.shown){c.beginPath();c.moveTo(x-10,y+9);c.lineTo(x+10,y-9);c.stroke()}}
      }
      ToolTip.visible:hovered;ToolTip.text:node.nodeKind==="environment"?(node.nodeVisible?"关闭此预览效果":"开启此预览效果"):(node.nodeVisible?"隐藏（同时停用生成）":"显示并启用")
      onClicked:editor.setNodeVisible(node.nodeId,!node.nodeVisible)
     }
    }
   }
  }
  ScrollView {
   id:details;objectName:"sceneDetailsScroll"
   SplitView.fillHeight:true;SplitView.minimumHeight:200;clip:true;contentWidth:availableWidth
   ColumnLayout {
    width:details.availableWidth;spacing:12
    property var selectedLayer:editor.layerDetails
    EnvironmentPanel{Layout.fillWidth:true;visible:editor.selectedNode.indexOf("environment")===0}
    TerrainPanel{Layout.fillWidth:true;visible:editor.terrainDetails.valid}
    ColumnLayout {
    Layout.fillWidth:true;visible:!editor.terrainDetails.valid && editor.selectedNode.indexOf("environment")!==0;spacing:12
    property var selectedLayer:editor.layerDetails
    Label{text:"详细信息";font.bold:true;Layout.margins:12;color:"#c9d2df"}
    TextField{text:parent.selectedLayer.name;Layout.fillWidth:true;Layout.margins:12;onEditingFinished:editor.renameSculptLayer(text)}
    Label{text:"类型   相对高度雕刻层";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"尺寸   "+parent.selectedLayer.width+" × "+parent.selectedLayer.depth+" 方块";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"原点   X "+parent.selectedLayer.originX+" / Z "+parent.selectedLayer.originZ;color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"高度   Y "+parent.selectedLayer.minY+" … "+parent.selectedLayer.maxY;color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"海平面   "+parent.selectedLayer.seaLevel; color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"种子   "+parent.selectedLayer.seed; color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"雕刻列   "+parent.selectedLayer.editedColumns;color:"#a6b4c5";Layout.leftMargin:12}
    Button{text:"移除此雕刻层";visible:parent.selectedLayer.removable;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.removeSculptLayer()}
    Label{text:"体素修改   "+parent.selectedLayer.overrideCells+" 格";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"当前雕刻层记录高度增量，实例隐藏后仍以相同增量叠加；隐藏的层不能雕刻。独立体素建筑/洞穴不随高度移动。";color:"#8997a9";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap}
    }
   }
  }
 }
}
