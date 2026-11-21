import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
ColumnLayout {
 id:panel;objectName:"environmentControls";spacing:10
 property var env:editor.environmentDetails
 property string selected:editor.selectedNode
 enabled:!editor.assetBusy
 function change(key,value){var data={};data[key]=value;editor.setEnvironment(data)}
 Label {text:"环境预览";font.pixelSize:16;Layout.margins:12}
 CheckBox {objectName:"environmentEnabled";text:"启用光照预览";checked:panel.env.enabled;Layout.leftMargin:8;onToggled:panel.change("enabled",checked)}
 Label {text:"影响视口显示，不改变地形或 Minecraft 导出。关闭后恢复编辑照明。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 Repeater {
  model:[{key:"sky",title:"天光"},{key:"sun",title:"太阳 · 平行光"},{key:"atmosphere",title:"大气 · 空气透视"}]
  ColumnLayout {
   required property var modelData
   Layout.fillWidth:true;spacing:8
   visible:panel.selected==="environment" || panel.selected==="environment-"+modelData.key
   CheckBox {text:modelData.title;checked:panel.env[modelData.key];Layout.leftMargin:8;onToggled:panel.change(modelData.key,checked)}
   Repeater {
    model:modelData.key==="sky" ? [{key:"skyIntensity",label:"天光强度 %",lo:0,hi:200}] : modelData.key==="sun" ? [{key:"azimuth",label:"方位角 °",lo:-180,hi:180},{key:"elevation",label:"仰角 °",lo:0,hi:90},{key:"sunIntensity",label:"太阳强度 %",lo:0,hi:300}] : [{key:"haze",label:"大气浓度 %",lo:0,hi:100}]
    RowLayout {
     required property var modelData
     Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
     Label{text:modelData.label;Layout.fillWidth:true}
     WorkbenchSpinBox {objectName:"environment_"+modelData.key;Layout.preferredWidth:130;from:modelData.lo;to:modelData.hi;value:panel.env[modelData.key];onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change(modelData.key,value)}
    }
   }
   Repeater {
    model:modelData.key==="sky" ? [{key:"skyColor",label:"天空颜色"},{key:"horizonColor",label:"地平线颜色"},{key:"groundColor",label:"地面反照颜色"}] : modelData.key==="sun" ? [{key:"sunColor",label:"太阳颜色"}] : []
    RowLayout {
     required property var modelData
     Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
     Label{text:modelData.label;Layout.fillWidth:true}
     Button {Layout.preferredWidth:130;text:panel.env[modelData.key];onClicked:{colorPicker.targetKey=modelData.key;colorPicker.selectedColor=panel.env[modelData.key];colorPicker.open()}
      background:Rectangle{radius:3;color:panel.env[modelData.key];border.color:"#6f8798"}
      contentItem:Text{text:parent.text;color:"#101722";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
     }
    }
   }
  }
 }
 RowLayout {Layout.fillWidth:true;Layout.margins:12
  Label{text:"曝光 · 0.1 EV";Layout.fillWidth:true}
  WorkbenchSpinBox{objectName:"environment_exposure";Layout.preferredWidth:130;from:-30;to:30;value:panel.env.exposure;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("exposure",value)}
 }
 Label{text:"大气浓度采用渐进曲线：低值适合全图观察，高值在近景也明显。0% 无雾，50% 约3606格衰减至37%，100% 约250格。需开启光照预览与大气。";visible:panel.selected==="environment"||panel.selected==="environment-atmosphere";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 Label{text:"本版为半球天光、直接太阳光和简化大气。投影阴影、体积云和体积雾将在后续版本加入。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 ColorDialog{id:colorPicker;property string targetKey;title:"选择环境颜色";options:ColorDialog.DontUseNativeDialog;onAccepted:panel.change(targetKey,selectedColor.toString())}
}
