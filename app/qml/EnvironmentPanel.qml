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
 Label {text:"光照与水面效果只影响预览，不改变地形或 Minecraft 导出。关闭光照后恢复编辑照明；水面可单独开关。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 Repeater {
  model:[{key:"sky",title:"天光"},{key:"sun",title:"太阳 · 平行光"},{key:"atmosphere",title:"大气 · 空气透视"}]
  ColumnLayout {
   required property var modelData
   Layout.fillWidth:true;spacing:8
   visible:panel.selected==="environment" || panel.selected==="environment-"+modelData.key
   CheckBox {text:modelData.title;checked:panel.env[modelData.key];Layout.leftMargin:8;onToggled:panel.change(modelData.key,checked)}
   CheckBox {visible:modelData.key==="sky";text:"天空云层";checked:panel.env.clouds;Layout.leftMargin:16;onToggled:panel.change("clouds",checked)}
   RowLayout {visible:modelData.key==="sky";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
    Label{text:"天空盒 / 球";Layout.fillWidth:true;elide:Text.ElideMiddle}
    Button {objectName:"environment_skyTexture";Layout.preferredWidth:130;text:panel.env.skyTexturePath ? "更换文件" : "选择文件";onClicked:skyFileDialog.open()}
    Button {visible:!!panel.env.skyTexturePath;Layout.preferredWidth:70;text:"清除";onClicked:panel.change("skyTexturePath","")}
   }
   Label {visible:modelData.key==="sky";text:panel.env.skyTexturePath ? panel.env.skyTexturePath : "未设置：使用程序化天空；支持2:1 PNG/JPG等经纬度天空图";Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12;elide:Text.ElideMiddle;color:"#99afbf"}
   CheckBox {objectName:"environment_shadows";visible:modelData.key==="sun";text:"太阳投影阴影";checked:panel.env.shadows;Layout.leftMargin:16;onToggled:panel.change("shadows",checked)}
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
     Button {Layout.preferredWidth:130;text:panel.env[modelData.key];onClicked:{colorPicker.targetKey=modelData.key;colorPicker.selectedColor=panel.env[modelData.key];colorPicker.open()}
      background:Rectangle{radius:3;color:panel.env[modelData.key];border.color:"#6f8798"}
      contentItem:Text{text:parent.text;color:"#101722";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
     }
    }
   }
  }
 }
 ColumnLayout {
  visible:panel.selected==="environment" || panel.selected==="environment-water"
  Layout.fillWidth:true;spacing:8
  CheckBox {objectName:"environment_water";text:"显示水面";checked:panel.env.water;Layout.leftMargin:8;onToggled:panel.change("water",checked)}
  CheckBox {objectName:"environment_waterWaves";text:"水面波纹";checked:panel.env.waterWaves;Layout.leftMargin:16;onToggled:panel.change("waterWaves",checked)}
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
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"深度透明度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waterDepthFade";Layout.preferredWidth:130;from:0;to:100;value:panel.env.waterDepthFade;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waterDepthFade",value)}
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"岸边泡沫强度 %";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waterFoamStrength";Layout.preferredWidth:130;from:0;to:100;value:panel.env.waterFoamStrength;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waterFoamStrength",value)}
  }
  RowLayout {Layout.fillWidth:true;Layout.leftMargin:12;Layout.rightMargin:12
   Label{text:"泡沫宽度 · 方块";Layout.fillWidth:true}
   WorkbenchSpinBox {returnFocusToViewport:true;objectName:"environment_waterFoamWidth";Layout.preferredWidth:130;from:1;to:32;value:panel.env.waterFoamWidth;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("waterFoamWidth",value)}
  }
  CheckBox {objectName:"environment_waterReflections";text:"水面反射";checked:panel.env.waterReflections;Layout.leftMargin:16;onToggled:panel.change("waterReflections",checked)}
  Label {text:"海平面使用半分辨率场景反射（长边最多1024像素），波纹会扰动采样坐标并自动过滤高频细节。深度透明度控制浅水到深水的吸收变化，岸边泡沫会沿地形水岸生成。不同高度的独立水方块仅使用天空反射。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 }
 RowLayout {Layout.fillWidth:true;Layout.margins:12
  Label{text:"曝光 · 0.1 EV";Layout.fillWidth:true}
  WorkbenchSpinBox{returnFocusToViewport:true;objectName:"environment_exposure";Layout.preferredWidth:130;from:-30;to:30;value:panel.env.exposure;onScrubStarted:editor.beginEnvironmentEdit();onScrubFinished:function(cancelled){editor.endEnvironmentEdit(cancelled)};onValueModified:panel.change("exposure",value)}
 }
 Label{text:"大气浓度采用渐进曲线：低值适合全图观察，高值在近景也明显。0% 无雾，50% 约3606格衰减至37%，100% 约250格。需开启光照预览与大气。";visible:panel.selected==="environment"||panel.selected==="environment-atmosphere";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 Label{text:"太阳阴影采用局部＋全图两级2048贴图与PCF过滤。天空云层使用球面坐标投影噪声，随时间缓慢移动并会进入水面反射；可选用本地2:1天空盒/球图替换背景。体积云雾和间接光照GI仍未实现。";Layout.fillWidth:true;Layout.margins:12;wrapMode:Text.WordWrap;color:"#99afbf"}
 ColorDialog{id:colorPicker;property string targetKey;title:"选择环境颜色";options:ColorDialog.DontUseNativeDialog;onAccepted:panel.change(targetKey,selectedColor.toString())}
 FileDialog{id:skyFileDialog;title:"选择经纬度天空图（2:1）";nameFilters:["天空图 (*.png *.jpg *.jpeg)","所有文件 (*)"];onAccepted:panel.change("skyTexturePath",selectedFile.toLocalFile())}
}
