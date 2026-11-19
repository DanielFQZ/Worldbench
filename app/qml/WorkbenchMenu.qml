import QtQuick
import QtQuick.Controls
Menu {
 id:control
 implicitWidth:310
 padding:4
 background:Rectangle{color:"#202b38";border.color:"#40576a";radius:4}
 delegate:MenuItem {
  id:row
  implicitHeight:36
  contentItem:Text{text:row.text;font:row.font;verticalAlignment:Text.AlignVCenter;leftPadding:10;rightPadding:10;color:!row.enabled?"#8194a7":row.highlighted?"#eaffff":"#dce4ee"}
  background:Rectangle{radius:3;color:row.highlighted&&row.enabled?"#15586e":"transparent";border.width:row.highlighted&&row.enabled?1:0;border.color:"#44aac0"}
 }
}
