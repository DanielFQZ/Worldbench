import "Theme.js" as Theme
import QtQuick
import QtQuick.Controls
ComboBox {
 id:control
 implicitHeight:Theme.controlHeight
 implicitWidth:160
 leftPadding:8;rightPadding:26;topPadding:4;bottomPadding:4
 contentItem:Text {text:control.displayText;font:control.font;color:control.enabled?Theme.text:Theme.disabled;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
 palette.text:Theme.text;palette.buttonText:Theme.text;palette.highlight:Theme.selection;palette.highlightedText:Theme.text
 background:Rectangle{color:control.down?Theme.selection:Theme.raised;border.color:control.activeFocus?Theme.accent:Theme.border;radius:3}
 indicator:Text{text:"▾";color:Theme.secondary;font.pixelSize:18;anchors.right:parent.right;anchors.rightMargin:10;anchors.verticalCenter:parent.verticalCenter}
 delegate:ItemDelegate {
  id:row;required property int index;required property var modelData
  width:control.width;implicitHeight:28
  highlighted:control.highlightedIndex===index
  contentItem:Text{text:control.textRole?modelData[control.textRole]:modelData;font:control.font;verticalAlignment:Text.AlignVCenter;color:row.highlighted?Theme.text:Theme.text;elide:Text.ElideRight}
  background:Rectangle{color:row.highlighted?Theme.selection:Theme.panel}
 }
 popup:Popup {
  y:control.height-1;width:control.width;padding:3
  implicitHeight:Math.min(contentItem.implicitHeight+6,320)
  background:Rectangle{color:Theme.panel;border.color:Theme.border;radius:3}
  contentItem:ListView {clip:true;implicitHeight:contentHeight;model:control.popup.visible?control.delegateModel:null;currentIndex:control.highlightedIndex;ScrollIndicator.vertical:ScrollIndicator{}}
 }
}
