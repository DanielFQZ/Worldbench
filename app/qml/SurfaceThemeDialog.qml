import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
 id:dialog;objectName:"surfaceThemeDialog"
 title:"地表高度主题";modal:true;width:880;height:570
 x:(parent.width-width)/2;y:Math.max(5,(parent.height-height)/2)
 property int materialRow:0
 property string materialRole:"surface"
 ListModel{id:bands}
 function reload(){
  const t=editor.surfaceTheme
  enabledBox.checked=t.enabled;beaches.checked=t.beaches
  edgeNoise.value=t.noise;edgeScale.value=t.scale;seed.value=t.seed
  bands.clear()
  for(var i=0;i<t.bands.length;i++)bands.append(t.bands[i])
 }
 function apply(){
  var rows=[]
  for(var i=0;i<bands.count;i++){const b=bands.get(i);rows.push({minHeight:b.minHeight,name:b.name,surface:b.surface,subsurface:b.subsurface,depth:b.depth})}
  if(editor.applySurfaceTheme({enabled:enabledBox.checked,beaches:beaches.checked,noise:edgeNoise.value,scale:edgeScale.value,seed:seed.value,bands:rows}))reload()
 }
 function chooseMaterial(row,role){materialRow=row;materialRole=role;search.text="";picker.catalog=editor.blockCatalog("");picker.open()}
 onOpened:reload()
 contentItem:ColumnLayout {
  spacing:12
  RowLayout{
   CheckBox{id:enabledBox;objectName:"themeEnabled";text:"启用高度主题"}
   CheckBox{id:beaches;text:"水位附近及以下使用沙滩材料"}
   Item{Layout.fillWidth:true}
  }
  Label{text:"每个区间从起始高度延伸到下一区间。材料随地表高度自动更新；独立涂绘、建筑和挖空优先保留。";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#9eb1c2"}
  RowLayout{
   spacing:8
   Label{text:"区间名称";Layout.preferredWidth:132}
   Label{text:"起始 Y";Layout.preferredWidth:115}
   Label{text:"表层方块";Layout.preferredWidth:170}
   Label{text:"下层方块";Layout.preferredWidth:170}
   Label{text:"总厚度（格）";Layout.preferredWidth:112}
  }
  ListView {
   id:ranges;objectName:"themeRanges";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:7;model:bands
   ScrollBar.vertical:ScrollBar{}
   delegate:RowLayout {
    required property int index
    required property string name
    required property int minHeight
    required property int surface
    required property int subsurface
    required property int depth
    id:row;width:ranges.width;spacing:8
    TextField{text:row.name;Layout.preferredWidth:132;selectByMouse:true;maximumLength:64;onTextEdited:bands.setProperty(row.index,"name",text)}
    WorkbenchSpinBox{objectName:"themeHeight"+row.index;from:-63;to:318;value:row.minHeight;Layout.preferredWidth:115;enabled:row.index>0;onValueModified:bands.setProperty(row.index,"minHeight",value)}
    Button{text:editor.themeMaterialLabel(row.surface);Layout.preferredWidth:170;onClicked:dialog.chooseMaterial(row.index,"surface");ToolTip.visible:hovered;ToolTip.text:"选择此区间的表层方块"}
    Button{text:editor.themeMaterialLabel(row.subsurface);Layout.preferredWidth:170;onClicked:dialog.chooseMaterial(row.index,"subsurface");ToolTip.visible:hovered;ToolTip.text:"选择表层下方的方块"}
    WorkbenchSpinBox{from:1;to:32;value:row.depth;Layout.preferredWidth:112;onValueModified:bands.setProperty(row.index,"depth",value)}
    Button{text:"×";Layout.preferredWidth:34;enabled:row.index>0;onClicked:bands.remove(row.index);ToolTip.visible:hovered;ToolTip.text:"删除此高度区间"}
   }
  }
  RowLayout {
   Button{objectName:"addThemeBand";text:"+ 高度区间";enabled:bands.count<16&&bands.count>0&&bands.get(bands.count-1).minHeight<318;onClicked:{const y=bands.get(bands.count-1).minHeight;bands.append({minHeight:Math.min(318,y+16),name:"新区间",surface:3,subsurface:3,depth:4})}}
   Label{text:"最多16段；首段从−63开始。总厚度包含表层，深处为石头。";color:"#8fa5b7";Layout.fillWidth:true;wrapMode:Text.WordWrap}
  }
  RowLayout {
   Label{text:"边界变化"}
   WorkbenchSpinBox{id:edgeNoise;objectName:"themeNoise";from:0;to:32;Layout.preferredWidth:112}
   Label{text:"格　尺度"}
   WorkbenchSpinBox{id:edgeScale;from:1;to:256;Layout.preferredWidth:112}
   Label{text:"种子"}
   WorkbenchSpinBox{id:seed;from:-1000000;to:1000000;Layout.preferredWidth:150}
   Item{Layout.fillWidth:true}
  }
  Label{text:"边界变化只影响材料分布，不改变山体高度。关闭主题可恢复默认地表；每次应用都可撤销。主题目前只使用完整、不透明方块的默认状态。";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:"#8fa5b7"}
  RowLayout {
   Item{Layout.fillWidth:true}
   Button{objectName:"applySurfaceTheme";text:"应用并预览";enabled:!editor.commandBusy&&!editor.exportBusy;onClicked:dialog.apply()}
   Button{text:"关闭";onClicked:dialog.close()}
  }
 }
 Dialog {
  id:picker;objectName:"themeMaterialPicker";title:"选择主题方块";modal:true;width:440;height:490
  x:(dialog.width-width)/2;y:35
  property var catalog:[]
  contentItem:ColumnLayout {
   TextField{id:search;objectName:"themeMaterialSearch";Layout.fillWidth:true;placeholderText:"中文 / 拼音 / ID";selectByMouse:true;onTextEdited:searchTimer.restart()}
   Timer{id:searchTimer;interval:160;onTriggered:picker.catalog=editor.blockCatalog(search.text)}
   ListView {
    id:materials;objectName:"themeMaterialList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:picker.catalog
    ScrollBar.vertical:ScrollBar{}
    delegate:ItemDelegate {
     required property var modelData
     objectName:"themeMaterialRow_"+modelData.name
     width:materials.width;height:44
     onClicked:{const id=editor.resolveThemeMaterial(modelData.name);if(id>=0){bands.setProperty(dialog.materialRow,dialog.materialRole,id);picker.close()}}
     contentItem:RowLayout {
      Rectangle{Layout.preferredWidth:28;Layout.preferredHeight:28;color:modelData.color;Image{anchors.fill:parent;source:modelData.thumbnail;smooth:false}}
      ColumnLayout{Layout.fillWidth:true;spacing:0;Label{text:modelData.title;Layout.fillWidth:true;elide:Text.ElideRight}Label{text:modelData.name;Layout.fillWidth:true;elide:Text.ElideRight;font.pixelSize:10;color:"#90a7b9"}}
     }
    }
   }
  }
  standardButtons:Dialog.Close
 }
}
