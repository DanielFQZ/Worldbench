import QtQuick
import QtQuick.Controls
ComboBox {
 id:control
 palette.text:"#e0edf7";palette.buttonText:"#e0edf7";palette.highlight:"#15586e";palette.highlightedText:"#eaffff"
 background:Rectangle{color:control.down?"#15586e":"#283746";border.color:control.activeFocus?"#44aac0":"#486074";radius:3}
 indicator:Text{text:"▾";color:"#a8dce7";font.pixelSize:18;anchors.right:parent.right;anchors.rightMargin:10;anchors.verticalCenter:parent.verticalCenter}
 delegate:ItemDelegate {
  id:row;required property int index;required property var modelData
  width:control.width;implicitHeight:34
  highlighted:control.highlightedIndex===index
  contentItem:Text{text:control.textRole?modelData[control.textRole]:modelData;font:control.font;verticalAlignment:Text.AlignVCenter;color:row.highlighted?"#eaffff":"#dce4ee";elide:Text.ElideRight}
  background:Rectangle{color:row.highlighted?"#15586e":"#202b38"}
 }
 popup:Popup {
  y:control.height-1;width:control.width;padding:3
  implicitHeight:Math.min(contentItem.implicitHeight+6,320)
  background:Rectangle{color:"#202b38";border.color:"#40576a";radius:3}
  contentItem:ListView {clip:true;implicitHeight:contentHeight;model:control.popup.visible?control.delegateModel:null;currentIndex:control.highlightedIndex;ScrollIndicator.vertical:ScrollIndicator{}}
 }
}
