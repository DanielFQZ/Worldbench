import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
 objectName:"selectionSettings"
 spacing:12
 property var area:editor.selection
 Label{text:"矩形选区";font.pixelSize:16;Layout.margins:12}
 Label{text:"在地表按住左键拖出矩形。橙色边界表示选区；选区本身不会改变地形。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#a8bccb"}
 Label{text:area.valid?area.width+" × "+area.depth+" 格\nX "+area.minX+" … "+area.maxX+"\nZ "+area.minZ+" … "+area.maxZ:"尚未选择区域";Layout.margins:12;color:"#e9c485"}
 CheckBox{text:"笔刷只修改选区内";checked:editor.selectionOnly;enabled:area.valid;onToggled:editor.selectionOnly=checked;Layout.leftMargin:8}
 Button{text:"选择世界中心 64 × 64";Layout.fillWidth:true;Layout.margins:12;onClicked:{const d=editor.layerDetails;const x=d.originX+Math.floor(d.width/2),z=d.originZ+Math.floor(d.depth/2);editor.setSelection(x-32,z-32,x+31,z+31)}}
 Button{text:"清除选区";enabled:area.valid;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.clearSelection()}
 Label{text:"批量抬升 / 降低";font.bold:true;Layout.leftMargin:12}
 Label{text:"作用于当前雕刻层；正数抬升、负数降低。最多262144列，独立方块保持原位。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#91a8ba"}
 WorkbenchSpinBox{id:heightOffset;objectName:"selectionHeightOffset";from:-381;to:381;value:4;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12}
 Button{objectName:"applySelectionHeight";text:"应用到选区";enabled:area.valid&&!editor.assetBusy&&!editor.exportBusy&&heightOffset.value!==0;Layout.fillWidth:true;Layout.margins:12;onClicked:editor.adjustSelectedHeight(heightOffset.value)}
 Label{text:"选好后可切换地形工具继续编辑。导出窗口可仅导出此选区覆盖的完整区块；未导出的区域不含自定义地形。选区只在本次会话中保存。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#91a8ba"}
}
