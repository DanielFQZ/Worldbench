import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
 id:panel
 objectName:"selectionSettings"
 spacing:12
 property var area:editor.selection
 Label{text:"矩形选区";font.pixelSize:16;Layout.margins:12}
 property var bounds:editor.layerDetails
 property string selectionKey:[area.valid,area.minX,area.minZ,area.maxX,area.maxZ,bounds.originX,bounds.originZ,bounds.width,bounds.depth].join(":")
 property var coordinates:[0,0,0,0]
 onSelectionKeyChanged:coordinates=area.valid?[area.minX,area.minZ,area.maxX,area.maxZ]:[bounds.originX,bounds.originZ,bounds.originX+bounds.width-1,bounds.originZ+bounds.depth-1]
 Label{text:"近景或全图视角均可按住左键框选。橙色边界和淡色填充表示实际选区；拾取使用精确世界数据，不读取粗LOD高度。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#a8bccb"}
 Label{text:area.valid?area.width+" × "+area.depth+" 格\nX "+area.minX+" … "+area.maxX+"\nZ "+area.minZ+" … "+area.maxZ:"尚未选择区域";Layout.margins:12;color:"#e9c485"}
 CheckBox{text:"笔刷只修改选区内";checked:editor.selectionOnly;enabled:area.valid;onToggled:editor.selectionOnly=checked;Layout.leftMargin:8}
 Button{text:"选择世界中心 64 × 64";Layout.fillWidth:true;Layout.margins:12;onClicked:{const d=editor.layerDetails;const x=d.originX+Math.floor(d.width/2),z=d.originZ+Math.floor(d.depth/2);editor.setSelection(x-32,z-32,x+31,z+31)}}
 Button{objectName:"selectWholeMap";text:"选择全图";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:editor.setSelection(bounds.originX,bounds.originZ,bounds.originX+bounds.width-1,bounds.originZ+bounds.depth-1)}
 Label{text:"精确范围 · 包含端点";font.bold:true;Layout.leftMargin:12}
 GridLayout {
  columns:3;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"轴"} Label{text:"起点"} Label{text:"终点"}
  Label{text:"X";color:"#ed7c7c"}
  WorkbenchSpinBox{returnFocusToViewport:true;id:minX;objectName:"selectionMinX";compact:true;Layout.fillWidth:true;from:panel.bounds.originX;to:from+panel.bounds.width-1;value:panel.coordinates[0]}
  WorkbenchSpinBox{returnFocusToViewport:true;id:maxX;objectName:"selectionMaxX";compact:true;Layout.fillWidth:true;from:minX.from;to:minX.to;value:panel.coordinates[2]}
  Label{text:"Z";color:"#77aaf0"}
  WorkbenchSpinBox{returnFocusToViewport:true;id:minZ;objectName:"selectionMinZ";compact:true;Layout.fillWidth:true;from:panel.bounds.originZ;to:from+panel.bounds.depth-1;value:panel.coordinates[1]}
  WorkbenchSpinBox{returnFocusToViewport:true;id:maxZ;objectName:"selectionMaxZ";compact:true;Layout.fillWidth:true;from:minZ.from;to:minZ.to;value:panel.coordinates[3]}
 }
 Button{objectName:"applySelectionBounds";text:"应用坐标范围";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:editor.setSelection(minX.value,minZ.value,maxX.value,maxZ.value)}
 Button{objectName:"focusSelection";text:"进入选区中心编辑";enabled:area.valid;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:if(editor.cameraController)editor.cameraController.jumpTo(Math.floor((area.minX+area.maxX)/2),Math.floor((area.minZ+area.maxZ)/2))}
 Button{text:"清除选区";enabled:area.valid;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.clearSelection()}
 Label{text:"批量抬升 / 降低";font.bold:true;Layout.leftMargin:12}
 Label{text:"作用于当前雕刻层；正数抬升、负数降低。单次最多"+editor.regionColumnLimit+"列（1024×1024），快照估算预算256 MiB。后台按区块计算，可从底栏取消；完成后统一应用为一次撤销。独立方块保持原位。"+(area.valid&&area.width*area.depth>editor.regionColumnLimit?"\n当前选区超出上限，请缩小范围。全图选区仍可用于导出。":"");Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#91a8ba"}
 WorkbenchSpinBox{returnFocusToViewport:true;id:heightOffset;objectName:"selectionHeightOffset";from:-381;to:381;value:4;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}
 Button{objectName:"applySelectionHeight";text:"应用到选区";enabled:area.valid&&area.width*area.depth<=editor.regionColumnLimit&&!editor.assetBusy&&!editor.exportBusy&&heightOffset.value!==0;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.adjustSelectedHeight(heightOffset.value)}
 Label{text:"大范围操作会占用撤销预算，旧历史可能被淘汰。当前工程仍为v8单文件；接近容量上限时请先另存备份。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#b9a781"}
 Label{text:"选好后可切换地形工具继续编辑。导出窗口可仅导出此选区覆盖的完整区块；未导出的区域不含自定义地形。选区只在本次会话中保存。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#91a8ba"}
}
