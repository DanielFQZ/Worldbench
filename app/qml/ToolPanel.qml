import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
 id:panel;spacing:10
 property var catalog:[]
 property bool usesMaterial:editor.tool>=2
 function refresh(){catalog=editor.blockCatalog(search.text)}
 Component.onCompleted:refresh()
 Connections{target:editor;function onCatalogChanged(){panel.refresh()}}
 Label{text:"笔刷设置";font.pixelSize:16;Layout.leftMargin:12;Layout.topMargin:14}
 Label{text:editor.tool<0?"选择模式 · 左键不修改地形":["抬升 / 降低","整平地表","体积挖除","体积填充","材质涂绘"][editor.tool];color:"#79c2c8";Layout.leftMargin:12}
 RowLayout{
  Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"半径";color:"#a6b4c5"}
  Slider{Layout.fillWidth:true;from:1;to:16;stepSize:1;value:editor.radius;onMoved:editor.radius=value}
  Label{text:editor.radius;Layout.preferredWidth:22}
 }
 RowLayout{
  visible:editor.tool===0||editor.tool===1
  Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"步进";color:"#a6b4c5"}
  Slider{Layout.fillWidth:true;from:1;to:4;stepSize:1;value:editor.strength;onMoved:editor.strength=value}
  Label{text:editor.strength;Layout.preferredWidth:22}
 }
 Label{visible:panel.usesMaterial;text:"填充 / 涂绘材质";font.bold:true;Layout.leftMargin:12;Layout.topMargin:8}
 TextField{id:search;visible:panel.usesMaterial;objectName:"materialSearch";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;placeholderText:"搜索中文名称或 minecraft:id";selectByMouse:true;onTextEdited:searchTimer.restart()}
 Timer{id:searchTimer;interval:180;onTriggered:panel.refresh()}
 Label{visible:panel.usesMaterial;text:panel.catalog.length+" 个匹配方块";color:"#8997a9";font.pixelSize:11;Layout.leftMargin:12}
 ListView {
  id:materials;visible:panel.usesMaterial;Layout.fillWidth:true;Layout.preferredHeight:240;clip:true;model:panel.catalog
  ScrollBar.vertical:ScrollBar{}
  delegate:ItemDelegate {
   required property var modelData
   width:materials.width;height:48;highlighted:editor.materialLabel.split("[")[0]===modelData.name
   background:Rectangle { color:parent.highlighted?"#344957":parent.hovered?"#2d343e":"transparent" }
   onClicked:editor.selectBlock(modelData.name)
   ToolTip.visible:hovered;ToolTip.delay:700;ToolTip.text:modelData.name+(modelData.diagnostic?"\n"+modelData.diagnostic:"")
   contentItem:RowLayout {
    spacing:10
    Rectangle{
     Layout.preferredWidth:30;Layout.preferredHeight:30;color:modelData.color;radius:2
     Image{anchors.fill:parent;source:modelData.thumbnail;smooth:false;fillMode:Image.PreserveAspectFit}
     Label{visible:modelData.fallback;text:"!";color:"#ff95ce";anchors.centerIn:parent}
    }
    ColumnLayout{
     spacing:1;Layout.fillWidth:true
     Label{text:modelData.title;elide:Text.ElideRight;Layout.fillWidth:true;font.pixelSize:12}
     Label{text:modelData.name;elide:Text.ElideRight;Layout.fillWidth:true;color:"#8494a7";font.pixelSize:10}
    }
   }
  }
 }
 Label{visible:panel.usesMaterial;text:editor.materialLabel;wrapMode:Text.WrapAnywhere;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;color:"#94c9cf";font.pixelSize:11}
 Label{text:editor.tool<0?"": ["左键抬升地表，Shift + 左键降低。", "左键将笔刷范围整平至命中高度。", "左键挖除体素，Shift + 左键用所选材质回填。", "左键填充所选材质，Shift + 左键挖除。", "左键涂绘表面材质。"][editor.tool];Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#8997a9"}
 Repeater{
  model:panel.usesMaterial?editor.materialProperties:[]
  delegate:RowLayout{
   required property var modelData
   Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:modelData.key;Layout.fillWidth:true;color:"#9caabb"}
   ComboBox{model:modelData.values;currentIndex:modelData.values.indexOf(modelData.value);Layout.preferredWidth:145;onActivated:editor.setBlockProperty(modelData.key,currentText)}
  }
 }
 Label{visible:panel.usesMaterial;text:"模型状态按所选值放置；门的上下半、栅栏连接需分别指定，当前不模拟游戏邻居更新。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#8997a9";font.pixelSize:11}
}
