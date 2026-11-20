import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
 id:row
 property string caption
 property var keys:[]
 property var values:[]
 property int minimum:-2000000
 property int maximum:2000000
 property var minimums:[]
 property var maximums:[]
 spacing:4
 Label{text:row.caption;color:"#aabacb"}
 RowLayout {
  spacing:5;Layout.fillWidth:true
  Repeater {
   model:3
   delegate:ColumnLayout {
    required property int index
    Layout.fillWidth:true;Layout.preferredWidth:1;spacing:2
    Label{text:["X","Y","Z"][index];color:["#ef6666","#76d789","#70a9ff"][index];font.pixelSize:11}
    WorkbenchSpinBox {
     objectName:row.keys[index]==="centerX"?"terrainX":"terrain_"+row.keys[index]
     compact:true;Layout.fillWidth:true;Layout.minimumWidth:60
     axisColor:["#ba5050","#4b9c60","#4b78b5"][index]
     from:row.minimums.length?row.minimums[index]:row.minimum;to:row.maximums.length?row.maximums[index]:row.maximum;value:row.values[index]||0
     onValueModified:editor.setTerrainOption(row.keys[index],value)
     onScrubStarted:editor.beginTerrainEdit()
     onScrubFinished:function(cancelled){editor.endTerrainEdit(cancelled)}
    }
   }
  }
 }
}
