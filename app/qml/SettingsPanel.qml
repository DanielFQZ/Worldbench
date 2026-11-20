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
     CheckBox{text:"飞行惯性（平滑加速 / 减速）";checked:editor.cameraController?editor.cameraController.flightInertia:true;onToggled:if(editor.cameraController)editor.cameraController.flightInertia=checked}
     Label{text:"松开方向键时短暂减速；松开右键、Esc 或切换窗口立即停止。";wrapMode:Text.WordWrap;Layout.fillWidth:true;color:"#9caabb"}
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
     Label{text:editor.assetStage;visible:editor.assetStage.length>0;Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#76d3e2"}
     ProgressBar{
      id:resourceProgress;objectName:"resourceProgress";Layout.fillWidth:true;visible:editor.assetStage.length>0;from:0;to:100;value:editor.assetProgress
      background:Rectangle{implicitHeight:8;color:"#131d28";radius:4}
      contentItem:Item{implicitHeight:8;Rectangle{width:parent.width*resourceProgress.visualPosition;height:parent.height;color:"#43bace";radius:4}}
     }
     Label{visible:editor.assetBusy;text:"解析进度 "+editor.assetProgress+"% · 可以移动或关闭此窗口，后台任务会继续";color:"#a5bacb";wrapMode:Text.WordWrap;Layout.fillWidth:true}
     ScrollView {
      visible:editor.assetLog.length>0;Layout.fillWidth:true;Layout.preferredHeight:180;clip:true
      TextArea{objectName:"resourceLiveLog";text:editor.assetLog;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;font.pixelSize:11;color:"#a8c6d8";background:Rectangle{color:"#151f2a"} onTextChanged:cursorPosition=length}
     }
     Label{text:editor.defaultResourceSources.length?"默认资源："+editor.defaultResourceSources.join("\n"):"尚未设置默认资源";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;color:"#91a8ba";font.pixelSize:11}
     Button{text:"清除默认资源设置";enabled:!editor.assetBusy&&editor.defaultResourceSources.length>0;onClicked:editor.clearDefaultResources()}
     Label{text:"手动成功导入或移除来源后会记为默认资源。打开工程优先使用工程自带来源；清除默认设置不移除当前工程资源。";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#91a8ba";font.pixelSize:11}

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
