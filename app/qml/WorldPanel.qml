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
 property size kddockwidgets_min_size:Qt.size(305,480)
 SplitView {
  anchors.fill:parent;orientation:Qt.Vertical
  ColumnLayout {
   SplitView.preferredHeight:220;SplitView.minimumHeight:130;spacing:2
   Label{text:"场景大纲";font.bold:true;color:"#c9d2df";Layout.margins:12}
   Repeater {
    model:[{node:"world",title:"◇  世界 / "+editor.projectName},{node:"terrain",title:"    ▧  地形基底"},{node:"voxels",title:"    ▦  体素编辑"},{node:"camera",title:"    ◉  透视相机"},{node:"resources",title:"    ▣  资源来源"}]
    delegate:ItemDelegate{
     required property var modelData
     Layout.fillWidth:true;implicitHeight:29;text:modelData.title;highlighted:editor.selectedNode===modelData.node
     background:Rectangle { color:parent.highlighted?"#344957":parent.hovered?"#2d343e":"transparent" }
     onClicked:editor.selectedNode=modelData.node
    }
   }
   Item{Layout.fillHeight:true}
  }
  ScrollView {
   SplitView.fillHeight:true;SplitView.minimumHeight:200;clip:true;contentWidth:availableWidth
   ColumnLayout {
    width:parent.width;spacing:12
    Label{text:"详细信息";font.bold:true;Layout.leftMargin:12;Layout.topMargin:12;color:"#c9d2df"}
    ColumnLayout {
     visible:editor.selectedNode==="world";Layout.fillWidth:true;Layout.margins:12;spacing:12
     Label{text:editor.projectName;font.pixelSize:18;font.bold:true}
     Label{text:editor.sceneStats;wrapMode:Text.WordWrap;Layout.fillWidth:true}
     Label{text:"Java 1.20.1 · Y −64…319\n128 × 128 原型场景\nMCP 与人工编辑共享撤销历史。";color:"#9caabb";wrapMode:Text.WordWrap;Layout.fillWidth:true}
     Label{text:"右键 + WASDQE 漫游\nAlt + 左键环绕 / 中键平移\n右键 + 滚轮调速 / F 聚焦\n雕刻时 Shift 反向 / [ ] 半径";color:"#9caabb";lineHeight:1.5}
    }
    ToolPanel{visible:editor.selectedNode==="terrain"||editor.selectedNode==="voxels";Layout.fillWidth:true}
    ColumnLayout {
     visible:editor.selectedNode==="camera";Layout.fillWidth:true;Layout.margins:12;spacing:12
     Label{text:"透视相机";font.pixelSize:17}
     Label{text:editor.cameraController?editor.cameraController.cameraText:"";wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#a6b4c5"}
     CheckBox{text:"Windows 原始鼠标输入";checked:editor.cameraController?editor.cameraController.rawInput:false;onToggled:if(editor.cameraController)editor.cameraController.rawInput=checked}
     Label{text:"旋转灵敏度  "+(editor.cameraController?editor.cameraController.sensitivity.toFixed(2):"")+" °/计数";color:"#a6b4c5"}
     Slider{Layout.fillWidth:true;from:.01;to:.5;stepSize:.01;value:editor.cameraController?editor.cameraController.sensitivity:.12;onMoved:if(editor.cameraController)editor.cameraController.sensitivity=value}
     Label{text:editor.cameraController&&editor.cameraController.rawActive?"原始输入：捕获中":"原始输入：导航时启用";color:"#74c2bd"}
     Label{text:"已接收原始输入包："+(editor.cameraController?editor.cameraController.rawPackets:0);color:"#9caabb"}
     Button{text:"重置视角";onClicked:if(editor.cameraController)editor.cameraController.resetCamera()}
     Label{text:"原始输入绕过系统指针加速。松开鼠标、Esc 或切出窗口会释放捕获。关闭时使用普通相对拖动，受桌面边界限制。";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#8997a9"}
    }
    ColumnLayout {
     visible:editor.selectedNode==="resources";Layout.fillWidth:true;Layout.margins:12;spacing:12
     Label{text:"Minecraft 资源";font.pixelSize:17}
     Label{text:editor.resourceSummary;wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#a6b4c5"}
     Repeater {
      model:editor.resourceSources
      delegate:ColumnLayout {
       required property string modelData;required property int index
       Layout.fillWidth:true
       Label{text:(index===0?"基础 · ":"覆盖 "+index+" · ")+modelData;wrapMode:Text.WrapAnywhere;Layout.fillWidth:true;font.pixelSize:11;color:"#c4ccd8"}
       Button{text:index===0?"清除所有来源":"移除此覆盖包";enabled:!editor.assetBusy&&!editor.exportBusy;onClicked:editor.removeResource(index)}
      }
     }
     Label{text:"从顶部“资源”菜单载入客户端 JAR，然后叠加 ZIP。后载入的包优先。\n专用实体渲染模型、动画及模组格式尚未支持；缺失模型显示洋红色方块。";color:"#8997a9";Layout.fillWidth:true;wrapMode:Text.WordWrap}
    }
    Item{height:12}
   }
  }
 }
}
