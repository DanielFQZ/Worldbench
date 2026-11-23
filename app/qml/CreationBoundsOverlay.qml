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
  onPaint: {
   let c=getContext("2d");c.reset()
   // Only creation bounds live here. Badges, handles and all transform gestures
   // use GizmoOverlay + ViewportGizmo, shared with heightmap instances.
   for(const o of root.objects) {
    c.lineWidth=o.selected?1.7:1;c.strokeStyle=o.selected?"#86dfc5":"#698b9a"
    for(const e of o.edges) {
     c.globalAlpha=e.inner?.35:.65;c.beginPath();c.moveTo(e.x1,e.y1);c.lineTo(e.x2,e.y2);c.stroke()
    }
   }
  }
 }
 DropArea {anchors.fill:parent;keys:["worldbench-asset","application/x-worldbench-asset"];onDropped:function(drop){let id=drop.source&&drop.source.assetId?drop.source.assetId:drop.getDataAsString("application/x-worldbench-asset");if(id){viewport.dropAsset(id,drop.x,drop.y);drop.acceptProposedAction()}}}
 Label {anchors.top:parent.top;anchors.topMargin:50;anchors.horizontalCenter:parent.horizontalCenter;visible:!!editor.placementAsset;text:editor.roadPlacement?"点击地表放置道路节点 · Esc 结束":"点击地表放置资产 · Esc 取消";color:"#ddf2e9";padding:8;background:Rectangle{color:"#d0344b4b";radius:4}}
}
