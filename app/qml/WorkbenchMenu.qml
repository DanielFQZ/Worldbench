import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
Menu {
 id:control
 implicitWidth:300
 padding:4
 background:Rectangle{color:Theme.panel;border.color:Theme.border;radius:4}
 delegate:MenuItem {
  id:row
  implicitHeight:30
  contentItem:Text{text:row.text;font:row.font;verticalAlignment:Text.AlignVCenter;leftPadding:10;rightPadding:10;color:!row.enabled?Theme.muted:row.highlighted?Theme.text:Theme.text}
  background:Rectangle{radius:3;color:row.highlighted&&row.enabled?Theme.selection:"transparent";border.width:row.highlighted&&row.enabled?1:0;border.color:Theme.accent}
 }
}
