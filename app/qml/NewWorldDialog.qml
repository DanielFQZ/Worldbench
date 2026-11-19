import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
 id:dialog;objectName:"newWorldDialog"
 title:"新建地形";modal:true;width:480
 standardButtons:Dialog.Ok|Dialog.Cancel
 contentItem:ColumnLayout {
  spacing:12
  Label{text:"Minecraft Java 1.20.1 · Y −64…319";color:"#99bccb"}
  GridLayout {
   columns:2;columnSpacing:16;rowSpacing:10;Layout.fillWidth:true
   Label{text:"地图尺寸"}
   WorkbenchComboBox{id:mapSize;model:[32,64,128,256];currentIndex:2;Layout.fillWidth:true}
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
  Label{text:"自动使用默认 Minecraft 资源。相同种子和参数生成相同地形。当前提供 32–256 格的小地图，后续扩展规模。";wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#9aafbf"}
 }
 onAccepted:editor.createWorld(Number(mapSize.currentText),seed.value,baseHeight.value,variation.value,seaLevel.value,terrainType.currentIndex===1)
}
