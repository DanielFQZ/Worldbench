import "Theme.js" as Theme
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
 WorkbenchCheckBox {objectName:"environmentEnabled";text:"启用光照预览";checked:panel.env.enabled;Layout.leftMargin:8;onToggled:panel.change("enabled",checked)}
 Label {text:"光照与水面效果只影响预览，不改变地形或 Minecraft 导出。关闭光照后恢复编辑照明；水面可单独开关。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:Theme.secondary}
 Repeater {
  model:[{key:"sky",title:"天光"},{key:"sun",title:"太阳 · 平行光"},{key:"atmosphere",title:"大气 · 空气透视"}]
  ColumnLayout {
   required property var modelData
   Layout.fillWidth:true;spacing:8
   visible:panel.selected==="environment" || panel.selected==="environment-"+modelData.key
   WorkbenchCheckBox {text:modelData.title;checked:panel.env[modelData.key];Layout.leftMargin:8;onToggled:panel.change(modelData.key,checked)}
   WorkbenchCheckBox {visible:modelData.key==="sky";text:"天空云层";checked:panel.env.clouds;Layout.leftMargin:16;onToggled:panel.change("clouds",checked)}
   RowLayout {visible:modelData.key==="sky";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
    Label{text:"天空盒 / 球";Layout.fillWidth:true;elide:Text.ElideMiddle}
    WorkbenchButton {objectName:"environment_skyTexture";Layout.preferredWidth:130;text:panel.env.skyTexturePath ? "更换文件" : "选择文件";onClicked:{skyFileDialog.currentFolder=editor.dialogFolder("skyTexture");skyFileDialog.open()}}
    WorkbenchButton {visible:!!panel.env.skyTexturePath;Layout.preferredWidth:70;text:"清除";onClicked:panel.change("skyTexturePath","")}
   }
   Label {visible:modelData.key==="sky";text:panel.env.skyTexturePath ? panel.env.skyTexturePath : "未设置：使用程序化天空；支持2:1 HDR/EXR/PNG/JPG/TGA等经纬度天空图";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;elide:Text.ElideMiddle;color:Theme.secondary}
   WorkbenchCheckBox {objectName:"environment_shadows";visible:modelData.key==="sun";text:"太阳投影阴影";checked:panel.env.shadows;Layout.leftMargin:16;onToggled:panel.change("shadows",checked)}
   Repeater {
    model:modelData.key==="sky" ? [{key:"skyIntensity",label:"天光强度 %",lo:0,hi:200},{key:"cloudDensity",label:"云层密度 %",lo:0,hi:100},{key:"cloudSpeed",label:"云层速度 %",lo:0,hi:100}] : modelData.key==="sun" ? [{key:"azimuth",label:"方位角 °",lo:-180,hi:180},{key:"elevation",label:"仰角 °",lo:0,hi:90},{key:"sunIntensity",label:"太阳强度 %",lo:0,hi:300}] : [{key:"haze",label:"大气浓度 %",lo:0,hi:100}]
    RowLayout {
     required property var modelData
     Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
     Label{text:modelData.label;Layout.fillWidth:true}
     WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_"+modelData.key;Layout.preferredWidth:130;from:modelData.lo;to:modelData.hi;value:panel.env[modelData.key];onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change(modelData.key,value)}
    }
   }
   Repeater {
    model:modelData.key==="sky" ? [{key:"skyColor",label:"天空颜色"},{key:"horizonColor",label:"地平线颜色"},{key:"groundColor",label:"地面反照颜色"}] : modelData.key==="sun" ? [{key:"sunColor",label:"太阳颜色"}] : []
    RowLayout {
     required property var modelData
     Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
     Label{text:modelData.label;Layout.fillWidth:true}
     WorkbenchButton {Layout.preferredWidth:130;text:panel.env[modelData.key];onClicked:{colorPicker.targetKey=modelData.key;colorPicker.selectedColor=panel.env[modelData.key];colorPicker.open()}
      background:Rectangle{radius:3;color:panel.env[modelData.key];border.color:Theme.muted}
      contentItem:Text{text:parent.text;color:"#101722";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
     }
    }
   }
  }
 }
 ColumnLayout {
  visible:panel.selected==="environment" || panel.selected==="environment-water"
  Layout.fillWidth:true;spacing:8
  WorkbenchCheckBox {objectName:"environment_water";text:"显示水面";checked:panel.env.water;Layout.leftMargin:8;onToggled:panel.change("water",checked)}
  WorkbenchCheckBox {objectName:"environment_waterWaves";text:"水面波纹";checked:panel.env.waterWaves;Layout.leftMargin:16;onToggled:panel.change("waterWaves",checked)}
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"波纹强度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waveStrength";Layout.preferredWidth:130;from:0;to:100;value:panel.env.waveStrength;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waveStrength",value)}
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"波纹密度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waveDensity";Layout.preferredWidth:130;from:1;to:100;value:panel.env.waveDensity;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waveDensity",value)}
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"波动速度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waveSpeed";Layout.preferredWidth:130;from:0;to:100;value:panel.env.waveSpeed;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waveSpeed",value)}
  }
  Repeater {
   model:[{key:"waterFogDensity",label:"水下雾浓度 · % / 米",lo:0,hi:200},{key:"waterFogStart",label:"水下雾起始距离 · 米",lo:0,hi:128}]
   RowLayout {required property var modelData;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
    Label{text:modelData.label;Layout.fillWidth:true}
    WorkbenchSpinBox {objectName:"environment_"+modelData.key;returnFocusToViewport:true;Layout.preferredWidth:130;from:modelData.lo;to:modelData.hi;value:panel.env[modelData.key];onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change(modelData.key,value)}
   }
  }
  Repeater {
   model:[{key:"waterFogColor",label:"水下雾颜色"},{key:"waterSurfaceColor",label:"水面颜色"},{key:"waterReflectionColor",label:"反射色调"},{key:"waterFoamColor",label:"岸边泡沫颜色"}]
   RowLayout {required property var modelData;Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
    Label{text:modelData.label;Layout.fillWidth:true}
    WorkbenchButton {Layout.preferredWidth:130;text:panel.env[modelData.key];onClicked:{colorPicker.targetKey=modelData.key;colorPicker.selectedColor=panel.env[modelData.key];colorPicker.open()}
     Rectangle{anchors.left:parent.left;anchors.leftMargin:8;anchors.verticalCenter:parent.verticalCenter;width:14;height:14;color:panel.env[modelData.key];border.color:Theme.border}
    }
   }
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"岸边泡沫强度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waterFoamStrength";Layout.preferredWidth:130;from:0;to:100;value:panel.env.waterFoamStrength;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waterFoamStrength",value)}
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"泡沫宽度 · 方块";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waterFoamWidth";Layout.preferredWidth:130;from:1;to:32;value:panel.env.waterFoamWidth;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waterFoamWidth",value)}
  }
  WorkbenchCheckBox {objectName:"environment_waterReflections";text:"水面反射";checked:panel.env.waterReflections;Layout.leftMargin:16;onToggled:panel.change("waterReflections",checked)}
  Label {text:"海平面使用半分辨率场景反射（长边最多1024像素），波纹会扰动采样坐标并自动过滤高频细节。水下雾按视线在水中的长度衰减，起始距离内保留清晰，之后逐渐呈现雾的颜色；可用于从岸边清水过渡到深水。1方块=1米，岸边泡沫会沿地形水岸生成。不同高度的独立水方块仅使用天空反射。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:Theme.secondary}
 }
 RowLayout {Layout.fillWidth:true;Layout.margins:12
  Label{text:"曝光 · 0.1 EV";Layout.fillWidth:true}
  WorkbenchSpinBox{returnFocusToViewport:true;objectName:"environment_exposure";Layout.preferredWidth:130;from:-30;to:30;value:panel.env.exposure;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("exposure",value)}
 }
 Label{text:"大气浓度采用渐进曲线：低值适合全图观察，高值在近景也明显。0% 无雾，50% 约3606格衰减至37%，100% 约250格。需开启光照预览与大气。";visible:panel.selected==="environment"||panel.selected==="environment-atmosphere";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:Theme.secondary}
 Label{text:"太阳阴影采用局部＋全图两级贴图，编辑→偏好设置可选近景1024/2048/4096分辨率与PCF过滤。天空云层使用球面坐标投影噪声，随时间缓慢移动并会进入水面反射；可选用本地2:1天空盒/球图替换背景。体积云雾和间接光照GI仍未实现。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:Theme.secondary}
 ColorDialog{id:colorPicker;property string targetKey;title:"选择环境颜色";options:ColorDialog.DontUseNativeDialog;onAccepted:panel.change(targetKey,selectedColor.toString())}
 FileDialog{id:skyFileDialog;objectName:"skyFileDialog";title:"选择经纬度天空图（2:1）";nameFilters:["天空图 (*.hdr *.exr *.png *.jpg *.jpeg *.tga *.bmp)","所有文件 (*)"];onRejected:editor.rememberDialogFolder("skyTexture",currentFolder,true);onAccepted:{editor.rememberDialogFolder("skyTexture",selectedFile);panel.change("skyTexturePath",selectedFile.toString())}}
}
