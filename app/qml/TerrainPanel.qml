import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
ColumnLayout {
 id:panel;objectName:"terrainInstanceControls";spacing:10
 property var instance:editor.terrainDetails
 enabled:!editor.assetBusy
 Label{text:"高度图地貌实例";font.pixelSize:16;Layout.margins:12}
 TextField{objectName:"terrainName";text:panel.instance.name||"";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;maximumLength:128;selectByMouse:true;onEditingFinished:if(panel.instance.valid)editor.setTerrainOption("name",text)}
 Label{text:(panel.instance.source||"")+"\n"+(panel.instance.pixelsX||0)+" × "+(panel.instance.pixelsZ||0)+" 像素 · 已嵌入工程";wrapMode:Text.WrapAnywhere;Layout.fillWidth:true;Layout.margins:12;color:"#99afbf"}
 CheckBox{text:"启用此实例";checked:panel.instance.enabled||false;Layout.leftMargin:8;onToggled:editor.setTerrainOption("enabled",checked)}
 AxisTriplet{Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;caption:"位置 · 中心 X/Z，竖直偏移 Y";keys:["centerX","y","centerZ"];values:[panel.instance.centerX,panel.instance.y,panel.instance.centerZ];minimums:[-2000000,-10000,-2000000];maximums:[2000000,10000,2000000]}
 AxisTriplet{Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;caption:"旋转 · 度（Y为竖直轴）";keys:["rotationX","rotationY","rotationZ"];values:[panel.instance.rotationX,panel.instance.rotationY,panel.instance.rotationZ];minimum:-180;maximum:180}
 AxisTriplet{Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;caption:"缩放 · %";keys:["scaleX","scaleY","scaleZ"];values:[panel.instance.scaleX,panel.instance.scaleY,panel.instance.scaleZ];minimum:1;maximum:1600}
 GridLayout {
  columns:2;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"宽度 X"} WorkbenchSpinBox{Layout.fillWidth:true;from:1;to:16384;value:panel.instance.width||1;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("width",value)}
  Label{text:"长度 Z"} WorkbenchSpinBox{Layout.fillWidth:true;from:1;to:16384;value:panel.instance.depth||1;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("depth",value)}
  Label{text:"黑色映射 Y"} WorkbenchSpinBox{Layout.fillWidth:true;from:-63;to:panel.instance.valid?panel.instance.high:318;value:panel.instance.low||0;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("low",value)}
  Label{text:"白色映射 Y"} WorkbenchSpinBox{Layout.fillWidth:true;from:panel.instance.valid?panel.instance.low:-63;to:318;value:panel.instance.high||0;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("high",value)}
  Label{text:"混合方式"} WorkbenchComboBox{Layout.fillWidth:true;model:["覆盖","叠加高度","取较高","取较低"];currentIndex:panel.instance.blend||0;onActivated:editor.setTerrainOption("blend",currentIndex)}
  Label{text:"影响强度 %"} WorkbenchSpinBox{Layout.fillWidth:true;from:0;to:100;value:panel.instance.opacity||0;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("opacity",value)}
  Label{text:"边缘羽化 %"} WorkbenchSpinBox{Layout.fillWidth:true;from:0;to:50;value:panel.instance.feather||0;onScrubStarted:editor.beginTerrainEdit();onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)};onValueModified:editor.setTerrainOption("feather",value)}
 }
 Label{text:"上方实例后计算；叠加模式把映射值作为高度增量。蒙版黑色不影响、白色完全影响。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 RowLayout{Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Button{text:"上移";enabled:panel.instance.canUp||false;Layout.fillWidth:true;onClicked:editor.moveTerrain(1)}
  Button{text:"下移";enabled:panel.instance.canDown||false;Layout.fillWidth:true;onClicked:editor.moveTerrain(-1)}
 }
 Button{text:"定位到实例中心";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:editor.focusTerrain()}
 Button{text:"导入 / 替换蒙版 PNG…";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:{maskDialog.currentFolder=editor.dialogFolder("terrainMask");maskDialog.open()}}
 Label{text:panel.instance.mask?"蒙版："+panel.instance.mask:"未设置蒙版";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;wrapMode:Text.WrapAnywhere;color:"#99afbf"}
 Button{text:"移除蒙版";enabled:!!panel.instance.mask;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:editor.clearTerrainMask()}
 Button{objectName:"removeTerrainButton";text:"移除实例";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;onClicked:editor.removeTerrain()}
 Label{text:"雕刻层记录相对高度；实例关闭后重新组合。旋转仍生成地表顶面，不产生悬空体。独立方块保持世界坐标。源图为8/16位灰度PNG；侵蚀与噪波后续加入。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 FileDialog{id:maskDialog;title:"选择灰度蒙版（白色影响、黑色保留）";fileMode:FileDialog.OpenFile;nameFilters:["灰度 PNG (*.png)"];onAccepted:{editor.rememberDialogFolder("terrainMask",selectedFile);editor.importHeightmap(selectedFile,true)}onRejected:editor.rememberDialogFolder("terrainMask",currentFolder,true)}
}
