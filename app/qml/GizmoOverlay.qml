import QtQuick
Canvas {
 id:canvas
 property var viewport
 property var overlay:viewport.gizmoOverlay
 onOverlayChanged:requestPaint()
 onWidthChanged:requestPaint()
 onHeightChanged:requestPaint()
 onPaint:{
  var c=getContext("2d");c.reset();c.clearRect(0,0,width,height)
  var axes=overlay.axes||[]
  for(var i=0;i<axes.length;i++) {
   var a=axes[i],p=a.points;c.strokeStyle=a.selected?"#fff1a4":a.color;c.fillStyle=c.strokeStyle;c.lineWidth=a.selected?4:2.5
   c.beginPath();for(var n=0;n<p.length;n++){if(n===0)c.moveTo(p[n].x,p[n].y);else c.lineTo(p[n].x,p[n].y)}c.stroke()
   if(overlay.mode!==1 && p.length>1){var end=p[p.length-1];if(overlay.mode===2)c.fillRect(end.x-5,end.y-5,10,10);else{c.beginPath();c.arc(end.x,end.y,5,0,Math.PI*2);c.fill()}c.font="bold 12px sans-serif";c.fillText(["X","Y","Z"][i],end.x+7,end.y-5)}
  }
  if(overlay.active&&overlay.mode!==1){c.fillStyle="#dbe9f1";c.fillRect(overlay.cx-4,overlay.cy-4,8,8)}
  var icons=overlay.markers||[]
  for(var j=0;j<icons.length;j++) {
   var m=icons[j],x=m.x,y=m.y-24;c.fillStyle=m.selected?"#27586c":"#202c3b";c.strokeStyle=m.enabled?"#82cfdd":"#74818d";c.lineWidth=m.selected?2:1
   c.beginPath();c.arc(x,y,12,0,Math.PI*2);c.fill();c.stroke()
   c.beginPath();c.moveTo(x-7,y+5);c.lineTo(x-1,y-5);c.lineTo(x+2,y);c.lineTo(x+5,y-3);c.lineTo(x+8,y+5);c.stroke()
   if(m.selected){c.font="12px sans-serif";c.fillStyle="#e0f5ff";c.fillText(m.name,x+17,y+4)}
  }
 }
}
