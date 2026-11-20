import QtQuick
import QtQuick.Controls
Item {
 id:root
 property var viewport
 property var overlay:viewport.gizmoOverlay
 onOverlayChanged:drawing.requestPaint()
 Canvas {
  id:drawing;anchors.fill:parent
  onWidthChanged:requestPaint();onHeightChanged:requestPaint()
  onPaint:{
   var c=getContext("2d");c.reset();c.clearRect(0,0,width,height)
   var o=root.overlay,axes=o.axes||[],planes=o.planes||[]
   function path(p){c.beginPath();for(var n=0;n<p.length;n++){if(n===0)c.moveTo(p[n].x,p[n].y);else c.lineTo(p[n].x,p[n].y)}}
   var sector=o.sector||[],ticks=o.ticks||[]
   if(sector.length){path(sector);c.closePath();c.fillStyle="#ffd469";c.globalAlpha=.22;c.fill();c.globalAlpha=1;c.strokeStyle="#ebca76";c.lineWidth=1.2;c.stroke()}
   c.strokeStyle="#b9a77e";c.lineWidth=1
   for(var t=0;t<ticks.length;t++){var tick=ticks[t];c.beginPath();c.moveTo(tick.x,tick.y);c.lineTo(tick.x2,tick.y2);c.stroke()}
   for(var j=0;j<planes.length;j++) {
    var plane=planes[j],active=o.dragging?o.axis===plane.axis:o.hover===plane.axis
    path(plane.points);c.closePath();c.fillStyle=active?"#b39e58":["#78733b","#665788","#397d77"][j];c.globalAlpha=active?.8:.45;c.fill();c.globalAlpha=1;c.strokeStyle=active?"#ffdf77":"#adc2ce";c.lineWidth=1;c.stroke()
   }
   for(var i=0;i<axes.length;i++) {
    var a=axes[i],p=a.points,chosen=a.selected
    c.strokeStyle=chosen?"#ffe283":a.color;c.fillStyle=c.strokeStyle;c.lineWidth=chosen?4:2.5
    path(p);c.stroke()
    if(o.mode===1) {
     c.font="bold 12px sans-serif";c.fillText(["X","Y","Z"][i],p[12].x+6,p[12].y-6)
    } else if(p.length>1) {
     var end=p[p.length-1],start=p[0],dx=end.x-start.x,dy=end.y-start.y,len=Math.sqrt(dx*dx+dy*dy)||1,ux=dx/len,uy=dy/len
     if(o.mode===2){c.fillRect(end.x-5,end.y-5,10,10);c.strokeStyle="#152432";c.lineWidth=1;c.strokeRect(end.x-5,end.y-5,10,10)}
     else {c.beginPath();c.moveTo(end.x+ux*5,end.y+uy*5);c.lineTo(end.x-ux*10-uy*5,end.y-uy*10+ux*5);c.lineTo(end.x-ux*10+uy*5,end.y-uy*10-ux*5);c.closePath();c.fill()}
     c.font="bold 13px sans-serif";c.fillText(["X","Y","Z"][i],end.x+9,end.y-7)
    }
   }
   if(o.active&&o.mode!==1){c.fillStyle=(o.dragging?o.axis===3:o.hover===3)?"#ffe283":"#dbe9f1";c.fillRect(o.cx-5,o.cy-5,10,10);c.strokeStyle="#172635";c.lineWidth=1;c.strokeRect(o.cx-5,o.cy-5,10,10)}
   var icons=o.markers||[]
   for(var n=0;n<icons.length;n++){
    var m=icons[n],x=m.x,y=m.y-24;c.fillStyle=m.selected?"#27586c":"#202c3b";c.strokeStyle=m.enabled?"#82cfdd":"#74818d";c.lineWidth=m.selected?2:1
    c.beginPath();c.arc(x,y,12,0,Math.PI*2);c.fill();c.stroke();c.beginPath();c.moveTo(x-7,y+5);c.lineTo(x-1,y-5);c.lineTo(x+2,y);c.lineTo(x+5,y-3);c.lineTo(x+8,y+5);c.stroke()
    if(m.selected){c.font="12px sans-serif";c.fillStyle="#e0f5ff";c.fillText(m.name,x+17,y+4)}
   }
  }
 }
 Rectangle {
  objectName:"transformReadout";visible:(root.overlay.dragging||false)&&(root.overlay.readout||"").length>0
  x:Math.max(5,Math.min(parent.width-width-5,(root.overlay.dragX||0)+22));y:Math.max(48,Math.min(parent.height-height-65,(root.overlay.dragY||0)+25))
  width:readout.implicitWidth+14;height:readout.implicitHeight+10;radius:3;color:"#ee152332";border.color:"#bda967"
  Text{id:readout;anchors.centerIn:parent;text:root.overlay.readout||"";color:"#fff1c7";font.pixelSize:11}
 }
}
