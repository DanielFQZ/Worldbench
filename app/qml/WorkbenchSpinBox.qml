import QtQuick
import QtQuick.Controls
import "Arithmetic.js" as Arithmetic
SpinBox {
 id:control
 property bool compact:false
 property color axisColor:"transparent"
 property real dragStep:1
 signal scrubStarted()
 signal scrubFinished(bool cancelled)
 property bool scrubbing:false
 function commitDrag(v){var n=Math.max(from,Math.min(to,Math.round(v)));if(n!==value){var oldStep=stepSize;stepSize=Math.abs(n-value);if(n>value)increase();else decrease();stepSize=oldStep;valueModified()}}
 editable:true
 textFromValue:function(value,locale){return String(value)}
 valueFromText:function(text,locale){var n=Arithmetic.calculate(text);return isFinite(n)?Math.max(from,Math.min(to,Math.round(n))):value}
 validator:RegularExpressionValidator{regularExpression:/[0-9+*/().\s-]{0,128}/}
 up.indicator.visible:!compact
 down.indicator.visible:!compact
 leftPadding:compact?0:down.indicator.width
 rightPadding:compact?0:up.indicator.width
 implicitWidth:compact?80:140
 background:Rectangle{color:"#191e26";border.color:control.axisColor!=="transparent"?control.axisColor:control.activeFocus?"#58b8ce":"#34404e";border.width:1}
 contentItem:TextInput {
  id:input
  z:2;padding:6;clip:width<implicitWidth
  text:control.displayText;font:control.font
  selectionColor:control.palette.highlight;selectedTextColor:control.palette.highlightedText
  horizontalAlignment:Qt.AlignHCenter;verticalAlignment:Qt.AlignVCenter
  readOnly:!control.editable;validator:control.validator
  inputMethodHints:Qt.ImhNoPredictiveText;selectByMouse:true
  property bool invalid:activeFocus&&text.length>0&&!isFinite(Arithmetic.calculate(text))
  color:invalid?"#ff967f":control.palette.text
  ToolTip.visible:invalid
  ToolTip.text:"请输入四则表达式，例如 (256+128)/2；不能除以零"
  onActiveFocusChanged:if(activeFocus)Qt.callLater(function(){if(!mouse.pressed)input.selectAll()})
  Keys.onEscapePressed:{if(control.scrubbing)mouse.finish(true);text=Qt.binding(function(){return control.displayText});focus=false}
  MouseArea {
   id:mouse;anchors.fill:parent;acceptedButtons:Qt.LeftButton;preventStealing:true
   property bool hadFocus:false
   property real startX:0
   property int startValue:0
   property int cancelValue:0
   property int anchor:0
   property bool selectText:false
   property bool doubleTap:false
   cursorShape:selectText?Qt.IBeamCursor:Qt.SizeHorCursor
   onPressed:function(e){doubleTap=false;hadFocus=input.activeFocus;startX=e.x;cancelValue=control.value;startValue=control.valueFromText(input.text,control.locale);anchor=input.positionAt(e.x,e.y);selectText=!!(e.modifiers&Qt.ShiftModifier);}
   onPositionChanged:function(e){
    if(!pressed)return
    if(!selectText&&Math.abs(e.x-startX)>4&&!control.scrubbing){control.scrubbing=true;control.scrubStarted();input.forceActiveFocus()}
    if(control.scrubbing){control.commitDrag(startValue+(e.x-startX)/3*control.dragStep)}
    else if(selectText&&Math.abs(e.x-startX)>3)input.select(anchor,input.positionAt(e.x,e.y))
   }
   function finish(cancel){if(control.scrubbing){if(cancel)control.commitDrag(cancelValue);control.scrubbing=false;control.scrubFinished(cancel)}}
   onReleased:function(e){
    if(control.scrubbing){finish(false);return}
    if(selectText&&Math.abs(e.x-startX)>3)return
    if(doubleTap){input.selectAll();return}
    input.forceActiveFocus()
    if(!hadFocus)Qt.callLater(function(){input.selectAll()})
    else if(Math.abs(e.x-startX)<3){input.deselect();input.cursorPosition=input.positionAt(e.x,e.y)}
   }
   onDoubleClicked:{doubleTap=true;input.selectAll()}
   onCanceled:finish(true)
  }
  Text{text:"⋮";x:3;anchors.verticalCenter:parent.verticalCenter;color:"#718399";font.pixelSize:10}
 }
}
