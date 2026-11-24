import QtQuick
import QtQuick.Controls
Item {
 id:root
 required property var viewport
 DropArea {anchors.fill:parent;keys:["worldbench-asset","application/x-worldbench-asset"];onDropped:function(drop){let id=drop.source&&drop.source.assetId?drop.source.assetId:drop.getDataAsString("application/x-worldbench-asset");if(id){viewport.dropAsset(id,drop.x,drop.y);drop.acceptProposedAction()}}}
 Label {anchors.top:parent.top;anchors.topMargin:50;anchors.horizontalCenter:parent.horizontalCenter;visible:!!editor.placementAsset;text:editor.roadPlacement?"点击地表放置道路节点 · Esc 结束":"点击地表放置资产 · Esc 取消";color:"#ddf2e9";padding:8;background:Rectangle{color:"#d0344b4b";radius:4}}
}
