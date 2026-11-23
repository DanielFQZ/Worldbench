import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Pane {
 padding:0
 background:Rectangle { color:Theme.panel }
 palette.window:Theme.panel; palette.windowText:Theme.text
 palette.base:Theme.field; palette.text:Theme.text
 palette.button:Theme.raised; palette.buttonText:Theme.text
 palette.highlight:Theme.selection; palette.highlightedText:Theme.text
 palette.placeholderText:Theme.muted
 property size kddockwidgets_min_size:Qt.size(280,400)
 SplitView {
  anchors.fill:parent;orientation:Qt.Vertical
  ColumnLayout {
   objectName:"sceneOutliner"
   SplitView.preferredHeight:250;SplitView.minimumHeight:120;spacing:2
   RowLayout{Layout.fillWidth:true;Layout.margins:8
    Label{text:"场景大纲";font.bold:true;color:Theme.text;Layout.fillWidth:true}
    WorkbenchButton {text:"+ 地形层";onClicked:editor.addSculptLayer();enabled:!editor.assetBusy}
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
    MouseArea {
     anchors.fill:parent;anchors.rightMargin:14;z:10;preventStealing:true
     property int pressedRow:-1
     property real pressY:0
     property real startY:0
     onPressed: function(mouse) { sceneList.cancelFlick(); pressedRow=sceneList.indexAt(mouse.x,mouse.y+sceneList.contentY);pressY=mouse.y;startY=sceneList.contentY }
     onPositionChanged:function(mouse){if(pressed && Math.abs(mouse.y-pressY)>5)sceneList.contentY=Math.max(0,Math.min(sceneList.contentHeight-sceneList.height,startY+pressY-mouse.y))}
     onReleased:function(mouse){if(Math.abs(mouse.y-pressY)>5||pressedRow<0||pressedRow>=sceneModel.count)return;const row=sceneModel.get(pressedRow);if(mouse.x>width-36)editor.setNodeVisible(row.nodeId,!row.nodeVisible);else editor.selectedNode=row.nodeId}
     onWheel:function(wheel){sceneList.cancelFlick();const dy=wheel.pixelDelta.y!==0?wheel.pixelDelta.y:wheel.angleDelta.y/3;sceneList.contentY=Math.max(0,Math.min(Math.max(0,sceneList.contentHeight-sceneList.height),sceneList.contentY-dy));wheel.accepted=true}
    }
    delegate:ItemDelegate{
     required property string nodeId
     required property string nodeName
     required property string nodeKind
     required property bool nodeVisible
     required property bool nodeActive
     id:node
     enabled:!editor.regionBusy
     width:ListView.view.width;implicitHeight:30;text:nodeName;rightPadding:38;highlighted:editor.selectedNode===nodeId
     background:Rectangle { color:parent.highlighted?Theme.selection:parent.hovered?Theme.hover:"transparent" }
     contentItem:RowLayout {
      spacing:8
      Canvas {
       Layout.preferredWidth:16;Layout.preferredHeight:16
       onPaint:{let c=getContext("2d");c.reset();c.strokeStyle=node.nodeKind==="environment"?Theme.warning:node.nodeActive?Theme.accent:Theme.secondary;c.lineWidth=1.2;c.beginPath();if(node.nodeKind==="environment"){c.arc(8,8,4,0,Math.PI*2);c.moveTo(8,0);c.lineTo(8,2);c.moveTo(8,14);c.lineTo(8,16)}else if(node.nodeKind==="heightmap"||node.nodeKind==="layer"){c.moveTo(1,13);c.lineTo(6,3);c.lineTo(10,10);c.lineTo(13,7);c.lineTo(16,13);c.closePath()}else{c.rect(2,2,12,12);c.moveTo(2,6);c.lineTo(14,6)}c.stroke()}
      }
      Text{text:node.nodeName;Layout.fillWidth:true;elide:Text.ElideRight;color:node.nodeVisible?Theme.text:Theme.muted;font.pixelSize:12}
     }
     onPressed: { if (sceneList.moving) sceneList.cancelFlick() }
     onClicked:editor.selectedNode=nodeId
     WorkbenchButton {
      id:eyeButton
      objectName:"eye_"+node.nodeId
      anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:30;height:28
      flat:true;leftPadding:0;rightPadding:0;topPadding:0;bottomPadding:0;enabled:!editor.assetBusy
      property bool shown:node.nodeVisible
      onShownChanged:eyeDrawing.requestPaint()
      contentItem:Canvas {
       id:eyeDrawing
       onPaint:{var c=getContext("2d");c.reset();c.strokeStyle=eyeButton.shown?Theme.text:Theme.muted;c.lineWidth=1.5;var x=width/2,y=height/2;c.beginPath();c.moveTo(x-9,y);c.quadraticCurveTo(x,y-10,x+9,y);c.quadraticCurveTo(x,y+10,x-9,y);c.stroke();c.beginPath();c.arc(x,y,3,0,Math.PI*2);c.stroke();if(!eyeButton.shown){c.beginPath();c.moveTo(x-10,y+9);c.lineTo(x+10,y-9);c.stroke()}}
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
    CreationDetails{Layout.fillWidth:true;visible:editor.creationDetails.valid}
    TerrainPanel{Layout.fillWidth:true;visible:editor.terrainDetails.valid}
    ColumnLayout {
    Layout.fillWidth:true;visible:!editor.creationDetails.valid && !editor.terrainDetails.valid && editor.selectedNode.indexOf("environment")!==0;spacing:12
    property var selectedLayer:editor.layerDetails
    Label{text:"详细信息";font.bold:true;Layout.margins:12;color:Theme.text}
    WorkbenchTextField {text:parent.selectedLayer.name;Layout.fillWidth:true;Layout.margins:12;onEditingFinished:editor.renameSculptLayer(text)}
    Label{text:"类型   相对高度雕刻层";color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"尺寸   "+parent.selectedLayer.width+" × "+parent.selectedLayer.depth+" 方块";color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"原点   X "+parent.selectedLayer.originX+" / Z "+parent.selectedLayer.originZ;color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"高度   Y "+parent.selectedLayer.minY+" … "+parent.selectedLayer.maxY;color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"海平面   "+parent.selectedLayer.seaLevel; color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"种子   "+parent.selectedLayer.seed; color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"雕刻列   "+parent.selectedLayer.editedColumns;color:Theme.secondary;Layout.leftMargin:12}
    WorkbenchButton {text:"移除此雕刻层";visible:parent.selectedLayer.removable;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.removeSculptLayer()}
    Label{text:"体素修改   "+parent.selectedLayer.overrideCells+" 格";color:Theme.secondary;Layout.leftMargin:12}
    Label{text:"当前雕刻层记录高度增量，实例隐藏后仍以相同增量叠加；隐藏的层不能雕刻。独立体素建筑/洞穴不随高度移动。";color:Theme.muted;Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap}
    }
   }
  }
 }
}
