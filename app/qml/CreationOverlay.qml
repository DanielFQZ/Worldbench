import QtQuick
import QtQuick.Controls
Item {
 id:root
 required property var viewport
 property var objects:viewport.creationOverlay
 onObjectsChanged:drawing.requestPaint()
 Connections{target:viewport;function onCameraChanged(){drawing.requestPaint()}}
 Canvas {
  id:drawing;anchors.fill:parent
  onPaint:{let c=getContext("2d");c.reset();for(const o of root.objects){c.lineWidth=o.selected?1.7:1;c.strokeStyle=o.selected?"#86dfc5":"#698b9a";for(const e of o.edges){c.globalAlpha=e.inner?.35:.65;c.beginPath();c.moveTo(e.x1,e.y1);c.lineTo(e.x2,e.y2);c.stroke()}c.globalAlpha=1;c.fillStyle=o.selected?"#9be5ce":"#648697";c.fillRect(o.x-7,o.y-7,14,14);c.fillStyle="#dae6f2";c.font='11px "Microsoft YaHei UI"';c.fillText(o.name,o.x+11,o.y-10);
    if(o.selected){for(let i=0;i<3;++i){if(viewport.gizmoMode===1&&(o.kind!=="asset"||i!==1))continue;if(o.kind==="road"&&viewport.gizmoMode!==0)continue;let a=o.axes[i];c.strokeStyle=["#ec7c78","#88d89a","#88baff"][i];c.fillStyle=c.strokeStyle;c.lineWidth=3;c.beginPath();c.moveTo(o.x,o.y);c.lineTo(a.x,a.y);c.stroke();if(viewport.gizmoMode===2)c.fillRect(a.x-5,a.y-5,10,10);else{c.beginPath();c.arc(a.x,a.y,5,0,Math.PI*2);c.fill()}c.fillText(["X","Y","Z"][i],a.x+7,a.y-5)}}}
  }
 }
 MouseArea {
  anchors.fill:parent;acceptedButtons:Qt.LeftButton
  property bool transforming:false
  property int axis:-1
  property real startX:0;property real startY:0
  property real dx:0;property real dy:0;property real worldLength:1
  onPressed:function(mouse){if(mouse.modifiers&Qt.AltModifier){mouse.accepted=false;return;}let selected=root.objects.find(o=>o.selected);axis=-1;if(selected)for(let i=0;i<3;++i)if(!(viewport.gizmoMode===1&&(selected.kind!=="asset"||i!==1))&&!(selected.kind==="road"&&viewport.gizmoMode!==0)&&Math.hypot(mouse.x-selected.axes[i].x,mouse.y-selected.axes[i].y)<15){axis=i;dx=selected.axes[i].x-selected.x;dy=selected.axes[i].y-selected.y;worldLength=selected.axes[i].length;break}
   if(axis>=0){startX=mouse.x;startY=mouse.y;transforming=true;editor.beginCreationTransform();viewport.forceActiveFocus();return}
   let object=root.objects.find(o=>Math.hypot(mouse.x-o.x,mouse.y-o.y)<14);if(object){editor.selectedNode=object.id;viewport.forceActiveFocus()}else mouse.accepted=false
  }
  onPositionChanged:function(mouse){if(!transforming)return;let fraction=((mouse.x-startX)*dx+(mouse.y-startY)*dy)/Math.max(1,dx*dx+dy*dy);let amount=viewport.gizmoMode===0?fraction*worldLength:viewport.gizmoMode===1?(mouse.x-startX)*2:fraction*100;editor.previewCreationTransform(axis,amount,viewport.gizmoMode)}
  onReleased:{if(transforming)editor.endCreationTransform();transforming=false}
  onCanceled:{if(transforming)editor.endCreationTransform(true);transforming=false}
 }
 DropArea {anchors.fill:parent;keys:["worldbench-asset","application/x-worldbench-asset"];onDropped:function(drop){let id=drop.source&&drop.source.assetId?drop.source.assetId:drop.getDataAsString("application/x-worldbench-asset");if(id){viewport.dropAsset(id,drop.x,drop.y);drop.acceptProposedAction()}}}
 Label {anchors.top:parent.top;anchors.topMargin:50;anchors.horizontalCenter:parent.horizontalCenter;visible:!!editor.placementAsset;text:editor.roadPlacement?"点击地表放置道路节点 · Esc 结束":"点击地表放置资产 · Esc 取消";color:"#ddf2e9";padding:8;background:Rectangle{color:"#d0344b4b";radius:4}}
}
