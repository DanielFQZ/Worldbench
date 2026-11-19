import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
 color: "#191c22"
 Column {
  anchors.top: parent.top; anchors.topMargin:12; anchors.horizontalCenter:parent.horizontalCenter; spacing:6
  Repeater {
   model:[{name:"选择 / 导航",tool:-1},{name:"抬升 / 降低",tool:0},{name:"整平地表",tool:1},{name:"体积挖除",tool:2},{name:"体积填充",tool:3},{name:"材质涂绘",tool:4},{name:"平滑地形",tool:5},{name:"矩形选区",tool:6}]
   delegate:Button {
    required property var modelData
    width:40; height:42; checkable:true; checked:editor.tool===modelData.tool
    ToolTip.visible:hovered; ToolTip.delay:500; ToolTip.text:modelData.name; Accessible.name:modelData.name
    onClicked:editor.tool=modelData.tool
    background:Rectangle {color:parent.checked?"#344957":parent.hovered?"#30353d":"transparent"; radius:5; border.width:parent.checked?1:0; border.color:"#6fbac5"}
    contentItem:Canvas {
     onPaint:{
      const c=getContext("2d");c.reset();c.strokeStyle="#dbe4ee";c.lineWidth=1.6;c.lineCap="round";c.lineJoin="round";c.translate(width/2-12,height/2-12);const t=modelData.tool;c.beginPath();
      if(t===-1){c.moveTo(5,2);c.lineTo(20,13);c.lineTo(13,14);c.lineTo(10,22);c.closePath();c.stroke()}
      else if(t===0){c.moveTo(1,20);c.lineTo(9,6);c.lineTo(15,16);c.lineTo(19,11);c.lineTo(24,20);c.stroke();c.beginPath();c.moveTo(18,2);c.lineTo(18,9);c.moveTo(15,5);c.lineTo(18,2);c.lineTo(21,5);c.stroke()}
      else if(t===1){c.moveTo(2,17);c.lineTo(7,10);c.lineTo(17,10);c.lineTo(23,17);c.moveTo(2,21);c.lineTo(23,21);c.stroke()}
      else if(t===2||t===3){c.rect(3,3,18,18);c.moveTo(7,12);c.lineTo(17,12);if(t===3){c.moveTo(12,7);c.lineTo(12,17)}c.stroke()}
      else if(t===6){c.setLineDash([3,2]);c.rect(2,3,21,18);c.stroke()}
      else if(t===5){c.moveTo(1,17);c.bezierCurveTo(7,2,16,2,23,17);c.moveTo(1,21);c.lineTo(23,21);c.stroke()}
      else {c.moveTo(7,16);c.lineTo(18,3);c.lineTo(22,7);c.lineTo(11,20);c.closePath();c.stroke();c.beginPath();c.moveTo(8,17);c.lineTo(3,21);c.lineTo(9,22);c.stroke()}
     }
    }
   }
  }
  Rectangle{width:28;height:1;color:"#3a4049";anchors.horizontalCenter:parent.horizontalCenter}
  ToolButton{width:40;height:40;text:"F";ToolTip.visible:hovered;ToolTip.text:"聚焦命中位置";onClicked:if(editor.cameraController)editor.cameraController.focusTarget()}
 }
}
