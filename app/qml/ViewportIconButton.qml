import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
WorkbenchButton {
 id: root
 property string glyph
 property string hint
 implicitWidth:30;implicitHeight:28;leftPadding:3;rightPadding:3;topPadding:2;bottomPadding:2
 hoverEnabled:true;focusPolicy:Qt.NoFocus
 Accessible.name:hint
 ToolTip.visible:hovered;ToolTip.delay:450;ToolTip.text:hint
 background:Rectangle{radius:4;color:root.checked?Theme.selection:root.hovered?Theme.hover:"transparent";border.color:root.checked?Theme.accent:"transparent"}
 onCheckedChanged:icon.requestPaint()
 contentItem:Canvas {
  id:icon
  onPaint:{
   var c=getContext("2d");c.reset();c.translate(width/2-12,height/2-12);c.strokeStyle=root.checked?Theme.text:Theme.text;c.fillStyle=c.strokeStyle;c.lineWidth=1.6;c.lineCap="round";c.lineJoin="round";c.beginPath()
   if(root.glyph==="move"){
    c.moveTo(3,12);c.lineTo(21,12);c.moveTo(12,3);c.lineTo(12,21);c.moveTo(6,9);c.lineTo(3,12);c.lineTo(6,15);c.moveTo(18,9);c.lineTo(21,12);c.lineTo(18,15);c.moveTo(9,6);c.lineTo(12,3);c.lineTo(15,6);c.moveTo(9,18);c.lineTo(12,21);c.lineTo(15,18);c.stroke()
   }else if(root.glyph==="rotate"){
    c.ellipse(2,7,20,10);c.stroke();c.beginPath();c.ellipse(7,2,10,20);c.stroke();c.beginPath();c.moveTo(19,4);c.lineTo(21,8);c.lineTo(17,8);c.stroke()
   }else if(root.glyph==="scale"){
    c.rect(3,14,7,7);c.moveTo(10,14);c.lineTo(20,4);c.moveTo(13,4);c.lineTo(20,4);c.lineTo(20,11);c.stroke()
   }else if(root.glyph==="focus"){
    c.moveTo(3,8);c.lineTo(3,3);c.lineTo(8,3);c.moveTo(16,3);c.lineTo(21,3);c.lineTo(21,8);c.moveTo(21,16);c.lineTo(21,21);c.lineTo(16,21);c.moveTo(8,21);c.lineTo(3,21);c.lineTo(3,16);c.moveTo(8,12);c.lineTo(16,12);c.moveTo(12,8);c.lineTo(12,16);c.stroke()
   }else if(root.glyph==="reset"){
    c.arc(12,12,8,-.9,4.1);c.stroke();c.beginPath();c.moveTo(3,6);c.lineTo(5,1);c.moveTo(3,6);c.lineTo(9,6);c.stroke()
   }else if(root.glyph==="sun"){
    c.arc(12,12,4,0,Math.PI*2);c.stroke();c.beginPath();for(var a=0;a<8;a++){var t=a*Math.PI/4;c.moveTo(12+7*Math.cos(t),12+7*Math.sin(t));c.lineTo(12+10*Math.cos(t),12+10*Math.sin(t))}c.stroke()
   }else if(root.glyph==="map"){
    c.moveTo(2,5);c.lineTo(8,2);c.lineTo(16,5);c.lineTo(22,2);c.lineTo(22,19);c.lineTo(16,22);c.lineTo(8,19);c.lineTo(2,22);c.closePath();c.moveTo(8,2);c.lineTo(8,19);c.moveTo(16,5);c.lineTo(16,22);c.stroke()
   }else if(root.glyph==="grid"){
    c.rect(3,3,18,18);for(var n=9;n<21;n+=6){c.moveTo(n,3);c.lineTo(n,21);c.moveTo(3,n);c.lineTo(21,n)}c.stroke()
   }else if(root.glyph==="stats"){
    c.moveTo(4,20);c.lineTo(4,13);c.moveTo(10,20);c.lineTo(10,5);c.moveTo(16,20);c.lineTo(16,10);c.moveTo(22,20);c.lineTo(22,2);c.stroke()
   }else if(root.glyph==="magnet"){
    c.moveTo(4,4);c.lineTo(4,14);c.bezierCurveTo(4,24,20,24,20,14);c.lineTo(20,4);c.lineTo(15,4);c.lineTo(15,14);c.bezierCurveTo(15,18,9,18,9,14);c.lineTo(9,4);c.closePath();c.stroke();c.beginPath();c.moveTo(4,8);c.lineTo(9,8);c.moveTo(15,8);c.lineTo(20,8);c.stroke()
   }
  }
 }
}
