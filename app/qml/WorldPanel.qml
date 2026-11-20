import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Pane {
 padding:0
 background:Rectangle { color:"#23262d" }
 palette.window:"#23262d"; palette.windowText:"#dce4ee"
 palette.base:"#191e26"; palette.text:"#dce4ee"
 palette.button:"#303741"; palette.buttonText:"#dce4ee"
 palette.highlight:"#344957"; palette.highlightedText:"#dce4ee"
 palette.placeholderText:"#7e8da1"
 property size kddockwidgets_min_size:Qt.size(280,400)
 SplitView {
  anchors.fill:parent;orientation:Qt.Vertical
  ColumnLayout {
   objectName:"sceneOutliner"
   SplitView.preferredHeight:220;SplitView.minimumHeight:120;spacing:2
   Label{text:"场景大纲";font.bold:true;color:"#c9d2df";Layout.margins:12}
   ListView {
    Layout.fillWidth:true;Layout.fillHeight:true;clip:true
    ScrollBar.vertical:ScrollBar{}
    model:editor.sceneNodes
    delegate:ItemDelegate{
     required property var modelData
     width:ListView.view.width;implicitHeight:34;text:"▧  "+modelData.name;highlighted:editor.selectedNode===modelData.id
     background:Rectangle { color:parent.highlighted?"#344957":parent.hovered?"#2d343e":"transparent" }
     onClicked:editor.selectedNode=modelData.id
    }
   }
  }
  ScrollView {
   id:details;objectName:"sceneDetailsScroll"
   SplitView.fillHeight:true;SplitView.minimumHeight:200;clip:true;contentWidth:availableWidth
   ColumnLayout {
    width:details.availableWidth;spacing:12
    property var selectedLayer:editor.layerDetails
    TerrainPanel{Layout.fillWidth:true;visible:editor.terrainDetails.valid}
    ColumnLayout {
    Layout.fillWidth:true;visible:!editor.terrainDetails.valid;spacing:12
    property var selectedLayer:editor.layerDetails
    Label{text:"详细信息";font.bold:true;Layout.margins:12;color:"#c9d2df"}
    Label{text:parent.selectedLayer.name;font.pixelSize:17;Layout.leftMargin:12}
    Label{text:"类型   基础地形层";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"尺寸   "+parent.selectedLayer.width+" × "+parent.selectedLayer.depth+" 方块";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"原点   X "+parent.selectedLayer.originX+" / Z "+parent.selectedLayer.originZ;color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"高度   Y "+parent.selectedLayer.minY+" … "+parent.selectedLayer.maxY;color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"海平面   "+parent.selectedLayer.seaLevel; color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"种子   "+parent.selectedLayer.seed; color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"体素修改   "+parent.selectedLayer.overrideCells+" 格";color:"#a6b4c5";Layout.leftMargin:12}
    Label{text:"此层包含当前地表与体素编辑结果。选择左侧工具进行编辑。";color:"#8997a9";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap}
    }
   }
  }
 }
}
