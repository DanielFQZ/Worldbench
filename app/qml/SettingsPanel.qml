import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ScrollView {
 id:panel
 property string page:"camera"
 clip:true;contentWidth:availableWidth
 ColumnLayout {
  width:panel.availableWidth;spacing:12
    ColumnLayout {
     visible:panel.page==="camera";Layout.fillWidth:true;Layout.margins:12;spacing:12
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
     visible:panel.page==="resources";Layout.fillWidth:true;Layout.margins:12;spacing:12
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
     Label{text:editor.resourceDiagnostics;wrapMode:Text.WrapAnywhere;Layout.fillWidth:true;color:"#d5ba82";font.pixelSize:11}
     Label{text:"从顶部“资源”菜单载入客户端 JAR，然后叠加 ZIP。后载入的包优先。\n专用实体渲染模型、动画及模组格式尚未支持；缺失模型显示洋红色方块。";color:"#8997a9";Layout.fillWidth:true;wrapMode:Text.WordWrap}
    }
 }
}
