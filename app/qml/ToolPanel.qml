import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
 id:panel;spacing:10
 property var catalog:[]
 property bool usesMaterial:editor.tool>=2&&editor.tool<=4
 property bool surfaceTool:editor.tool===0||editor.tool===1||editor.tool===5
 property var options:editor.brushOptions
 function refresh(){catalog=editor.blockCatalog(search.text)}
 Component.onCompleted:refresh()
 Connections{target:editor;function onCatalogChanged(){panel.refresh()}}
 Label{text:"笔刷设置";font.pixelSize:16;Layout.leftMargin:12;Layout.topMargin:14}
 Label{text:editor.tool<0?"选择模式 · 左键不修改地形":["抬升 / 降低","整平地表","体积挖除","体积填充","材质涂绘","平滑地形"][editor.tool];color:"#79c2c8";Layout.leftMargin:12}
 RowLayout{
  Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"半径";color:"#a6b4c5"}
  Slider{Layout.fillWidth:true;from:1;to:16;stepSize:1;value:editor.radius;onMoved:editor.radius=value}
  Label{text:editor.radius;Layout.preferredWidth:22}
 }
 RowLayout{
  visible:panel.surfaceTool
  Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"步进";color:"#a6b4c5"}
  Slider{Layout.fillWidth:true;from:1;to:4;stepSize:1;value:editor.strength;onMoved:editor.strength=value}
  Label{text:editor.strength;Layout.preferredWidth:22}
 }
 GridLayout {
  columns:2;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;columnSpacing:8
  Label{text:"形状";color:"#a6b4c5"}
  WorkbenchComboBox{Layout.fillWidth:true;model:panel.surfaceTool?["圆形","方形"]:["球形","立方体"];currentIndex:panel.options.shape;onActivated:editor.setBrushOption("shape",currentIndex)}
  Label{visible:panel.surfaceTool;text:"力度分布";color:"#a6b4c5"}
  WorkbenchComboBox{visible:panel.surfaceTool;Layout.fillWidth:true;model:["线性","恒定","柔和"];currentIndex:panel.options.falloff;onActivated:editor.setBrushOption("falloff",currentIndex)}
  Label{visible:editor.tool===1;text:"平整方向";color:"#a6b4c5"}
  WorkbenchComboBox{objectName:"flattenModeSelector";visible:editor.tool===1;Layout.fillWidth:true;model:["抬高和降低","仅抬高","仅降低"];currentIndex:panel.options.flattenMode;onActivated:editor.setBrushOption("flattenMode",currentIndex)}
 }
 CheckBox{objectName:"fillHolesOption";visible:editor.tool===1;Layout.leftMargin:8;text:"填充空洞";checked:panel.options.fillHoles;onToggled:editor.setBrushOption("fillHoles",checked)}
 RowLayout{
  objectName:"fillDepthControls";visible:editor.tool===1&&panel.options.fillHoles;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
  Label{text:"平面下方深度（格）";color:"#a6b4c5"}
  SpinBox{Layout.fillWidth:true;from:1;to:383;value:panel.options.fillDepth;editable:true;onValueModified:editor.setBrushOption("fillDepth",value)}
 }
 Label{visible:editor.tool===1;text:panel.options.fillHoles?"表面补齐一层方块；下方指定深度内的空气填为石头，更深空腔保留。":"只补齐表面的一层方块，下方空腔保留。";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;wrapMode:Text.WordWrap;color:"#91a8ba";font.pixelSize:11}
 CheckBox{Layout.leftMargin:8;text:"启用笔刷过滤";checked:panel.options.filterEnabled;onToggled:editor.setBrushOption("filterEnabled",checked)}
 ColumnLayout {
  visible:panel.options.filterEnabled;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;spacing:8
  Label{text:"高度范围 Y";color:"#a6b4c5"}
  RowLayout{
   SpinBox{Layout.fillWidth:true;from:-63;to:318;value:panel.options.minHeight;editable:true;onValueModified:editor.setBrushOption("minHeight",value)}
   Label{text:"至"}
   SpinBox{Layout.fillWidth:true;from:-63;to:318;value:panel.options.maxHeight;editable:true;onValueModified:editor.setBrushOption("maxHeight",value)}
  }
  Label{text:"地表坡度（度）";color:"#a6b4c5"}
  RowLayout{
   SpinBox{Layout.fillWidth:true;from:0;to:90;value:panel.options.minSlope;editable:true;onValueModified:editor.setBrushOption("minSlope",value)}
   Label{text:"至"}
   SpinBox{Layout.fillWidth:true;from:0;to:90;value:panel.options.maxSlope;editable:true;onValueModified:editor.setBrushOption("maxSlope",value)}
  }
  WorkbenchComboBox{Layout.fillWidth:true;model:["不限材质","仅以下材质","排除以下材质"];currentIndex:panel.options.materialFilter;onActivated:editor.setBrushOption("materialFilter",currentIndex)}
  Label{visible:panel.options.materialFilter!==0;text:panel.options.filterBlock;Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;color:"#94c9cf";font.pixelSize:11}
  Button{visible:panel.options.materialFilter!==0;text:"使用当前涂绘材质作为条件";Layout.fillWidth:true;onClicked:editor.setBrushOption("filterBlock",editor.materialLabel.split("[")[0])}
  Label{text:"所有条件同时满足才会修改。坡度依据地形层高度；形状工具检测地表材质，体积工具逐格检测。";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#91a8ba";font.pixelSize:11}
 }
 Label{visible:panel.usesMaterial;text:"填充 / 涂绘材质";font.bold:true;Layout.leftMargin:12;Layout.topMargin:8}
 TextField{id:search;visible:panel.usesMaterial;objectName:"materialSearch";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;placeholderText:"中文 / 拼音 / 首字母 / ID";selectByMouse:true;onTextEdited:searchTimer.restart()}
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
 Label{text:editor.tool<0?"": ["左键抬升地表，Shift + 左键降低。", "左键将笔刷范围整平至命中高度。", "左键挖除体素，Shift + 左键用所选材质回填。", "左键填充所选材质，Shift + 左键挖除。", "左键涂绘表面材质。", "左键柔化局部高度差，保留体素阶梯外观与地下空腔。"][editor.tool];Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#8997a9"}
 Repeater{
  model:panel.usesMaterial?editor.materialProperties:[]
  delegate:RowLayout{
   required property var modelData
   Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:modelData.key;Layout.fillWidth:true;color:"#9caabb"}
   WorkbenchComboBox{model:modelData.values;currentIndex:modelData.values.indexOf(modelData.value);Layout.preferredWidth:145;onActivated:editor.setBlockProperty(modelData.key,currentText)}
  }
 }
 Label{visible:panel.usesMaterial;text:"模型状态按所选值放置；门的上下半、栅栏连接需分别指定，当前不模拟游戏邻居更新。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#8997a9";font.pixelSize:11}
}
