import QtQuick
import QtQuick.Controls
SpinBox {
 id:control
 editable:true
 textFromValue:function(value,locale){return String(value)}
 valueFromText:function(text,locale){return Number(text)}
 validator:IntValidator{bottom:Math.min(control.from,control.to);top:Math.max(control.from,control.to);locale:"C"}
 contentItem:TextInput {
  z:2;padding:6;clip:width<implicitWidth
  text:control.displayText;font:control.font;color:control.palette.text
  selectionColor:control.palette.highlight;selectedTextColor:control.palette.highlightedText
  horizontalAlignment:Qt.AlignHCenter;verticalAlignment:Qt.AlignVCenter
  readOnly:!control.editable;validator:control.validator
  inputMethodHints:Qt.ImhFormattedNumbersOnly;selectByMouse:true
  onActiveFocusChanged:if(activeFocus)Qt.callLater(selectAll)
  TapHandler{acceptedButtons:Qt.LeftButton;onTapped:Qt.callLater(function(){parent.forceActiveFocus();parent.selectAll()})}
 }
}
