import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
 id:dialog;objectName:"newWorldDialog"
 title:"新建地形";modal:true;width:520
 standardButtons:Dialog.Ok|Dialog.Cancel
 contentItem:ColumnLayout {
  spacing:12
  Label{text:"Minecraft Java 1.20.1 · Y −64…319";color:"#99bccb"}
  GridLayout {
   columns:2;columnSpacing:16;rowSpacing:10;Layout.fillWidth:true
   Label{text:"尺寸预设"}
   WorkbenchComboBox{id:mapSize;model:["128 × 128","256 × 256","512 × 512 · 实验","1024 × 1024 · 实验","2048 × 2048 · 实验","3072 × 3072 · 实验","4096 × 4096 · 实验","16384 × 16384 · 实验"];Layout.fillWidth:true;onActivated:{const sizes=[128,256,512,1024,2048,3072,4096,16384];mapWidth.value=sizes[currentIndex];mapDepth.value=sizes[currentIndex]}}
   Label{text:"宽度 X（格）"}
   SpinBox{id:mapWidth;objectName:"newMapWidth";from:32;to:16384;value:128;editable:true;Layout.fillWidth:true}
   Label{text:"长度 Z（格）"}
   SpinBox{id:mapDepth;objectName:"newMapDepth";from:32;to:16384;value:128;editable:true;Layout.fillWidth:true}
   Label{text:"地形类型"}
   WorkbenchComboBox{id:terrainType;model:["丘陵","平坦"];Layout.fillWidth:true}
   Label{text:"种子"}
   SpinBox{id:seed;from:-1000000;to:1000000;value:2026;editable:true;Layout.fillWidth:true}
   Label{text:"基础高度 Y"}
   SpinBox{id:baseHeight;from:-63;to:318;value:24;editable:true;Layout.fillWidth:true}
   Label{visible:terrainType.currentIndex===0;text:"起伏幅度"}
   SpinBox{id:variation;visible:terrainType.currentIndex===0;from:0;to:128;value:12;editable:true;Layout.fillWidth:true}
   Label{text:"海平面 Y"}
   SpinBox{id:seaLevel;from:-64;to:319;value:12;editable:true;Layout.fillWidth:true}
  }
  Label{text:"自动使用默认 Minecraft 资源。相同种子和参数生成相同地形。长宽可独立填写 32–16384 格。大于 256 格时按需精确预览附近最多 9×9 区块，远处暂不显示；可通过“视图 → 跳转至坐标”移动。完整远景后续加入。";wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#9aafbf"}
 }
 onAccepted:editor.createWorld(mapWidth.value,seed.value,baseHeight.value,variation.value,seaLevel.value,terrainType.currentIndex===1,mapDepth.value)
}
