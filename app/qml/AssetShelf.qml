import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "Theme.js" as Theme

Rectangle {
    id:root
    property var viewport
    // Qt delivers editor notifications during a drag. Keep delegates alive for
    // focus, double-click recognition and pointer grabs until the catalog changes.
    property string inventorySignature:JSON.stringify(editor.libraryAssets)
    readonly property var entries:JSON.parse(inventorySignature)
    property string folderSignature:JSON.stringify(editor.libraryFolders)
    readonly property var folders:JSON.parse(folderSignature)
    property var folder:null // null shows all assets; the empty string is the root.
    onFoldersChanged:if(folder && !folders.includes(folder)) folder=null
    property bool expanded:false
    signal editorRequested(string id)
    signal importRequested()
    signal graphRequested(bool material)
    function kindName(kind) {
        return ({graph:"PCG 图",range:"PCG 范围预设",plant:"植物",building:"建筑",cave:"洞穴",material:"地表材质",terrain:"地形实例"})[kind] || kind
    }
    color:Theme.window
    property string saveId:""
    property bool dragActive:false
    property bool dragCancelled:false
    Shortcut{sequence:"Esc";enabled:root.dragActive;onActivated:{root.dragCancelled=true;root.dragActive=false;root.viewport.clearAssetPreview()}}
    Menu {id:assetMenu;property string assetId;property string collection
        MenuItem{text:"重命名…";onTriggered:{
            nameDialog.makeFolder=false
            nameField.text=root.entries.find(a=>a.id===assetMenu.assetId).name
            nameDialog.open()
        }}
        Menu {
            id:moveMenu;title:"移至文件夹"
            MenuItem{text:"根目录";onTriggered:editor.moveLibraryAsset(assetMenu.assetId,"")}
            Instantiator {
                model:root.folders
                delegate:MenuItem {required property string modelData;text:modelData;onTriggered:editor.moveLibraryAsset(assetMenu.assetId,modelData)}
                onObjectAdded:(index,object)=>moveMenu.insertItem(index+1,object)
                onObjectRemoved:(index,object)=>moveMenu.removeItem(object)
            }
        }
        MenuItem{text:"编辑资产";onTriggered:root.editorRequested(assetMenu.assetId)}
        MenuItem{text:"导出资产…";visible:assetMenu.collection!=="terrains";onTriggered:{root.saveId=assetMenu.assetId;saveAsset.currentFolder=editor.dialogFolder("assetPresetSave");saveAsset.open()}}
        MenuItem{text:"从资产库删除";visible:assetMenu.collection!=="terrains";onTriggered:editor.creationEdit("remove",{kind:assetMenu.collection,id:assetMenu.assetId})}
    }
    Dialog {
        id:nameDialog;property bool makeFolder:false
        title:makeFolder?"新建文件夹":"重命名资产"
        parent:Overlay.overlay;anchors.centerIn:parent;modal:true
        standardButtons:Dialog.Ok|Dialog.Cancel
        onOpened:{nameField.forceActiveFocus();nameField.selectAll()}
        onAccepted:{
            let value=nameField.text.trim()
            if(makeFolder){let path=(root.folder?root.folder+"/":"")+value;if(value && !value.includes("/") && editor.createAssetFolder(path))root.folder=path}
            else editor.renameLibraryAsset(assetMenu.assetId,value)
        }
        WorkbenchTextField{id:nameField;width:260;maximumLength:128;onAccepted:nameDialog.accept()}
    }
    FileDialog{id:saveAsset;fileMode:FileDialog.SaveFile;nameFilters:assetMenu.collection==="graphs"?["Worldbench PCG (*.wbpcg)"]:["Worldbench 资产 (*.wbasset)"];onAccepted:{editor.rememberDialogFolder("assetPresetSave",selectedFile);editor.creationFile(assetMenu.collection==="graphs"?"save_graph":"save_asset",root.saveId,selectedFile)}
        onRejected:editor.rememberDialogFolder("assetPresetSave",currentFolder,true)}
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Rectangle {
            Layout.fillWidth:true;Layout.preferredHeight:30;color:Theme.panel
            RowLayout {
                anchors.fill:parent;anchors.leftMargin:6;anchors.rightMargin:8;spacing:8
                WorkbenchButton {objectName:"assetShelfToggle";text:(root.expanded?"▾  ":"▸  ")+"资产库";flat:true;implicitHeight:28;checkable:true;checked:root.expanded;onClicked:root.expanded=!root.expanded}
                Label{text:root.entries.length+" 项";color:Theme.muted;font.pixelSize:11}
                Item{Layout.fillWidth:true}
                WorkbenchTextField {id:search;visible:root.expanded;Layout.preferredWidth:180;implicitHeight:24;placeholderText:"搜索资产…"}
                WorkbenchButton {visible:root.expanded;text:"+ 新建";flat:true;implicitHeight:26;onClicked:createMenu.popup()
                    Menu{id:createMenu
                        MenuItem{text:"PCG 图";onTriggered:root.graphRequested(false)}
                        MenuItem{text:"地表材质";onTriggered:root.graphRequested(true)}
                    }
                }
                WorkbenchButton {visible:root.expanded;text:"导入…";flat:true;implicitHeight:26;onClicked:root.importRequested()}
            }
        }
        RowLayout {
            spacing:0
            visible:root.expanded;Layout.fillWidth:true;Layout.fillHeight:true
            Rectangle {
                Layout.preferredWidth:168;Layout.fillHeight:true;color:Theme.panel
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:4;spacing:1
                    RowLayout {
                        Layout.fillWidth:true
                        Label{text:"文件夹";color:Theme.secondary;Layout.fillWidth:true;leftPadding:5}
                        WorkbenchButton{text:"+";flat:true;implicitWidth:26;implicitHeight:24;Accessible.name:"新建子文件夹"
                            onClicked:{nameDialog.makeFolder=true;nameField.text="";nameDialog.open()}}
                        WorkbenchButton{text:"−";flat:true;implicitWidth:26;implicitHeight:24;enabled:!!root.folder;Accessible.name:"删除空文件夹"
                            onClicked:editor.removeAssetFolder(root.folder)}
                    }
                    ListView {
                        Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                        model:[null,""].concat(root.folders)
                        ScrollBar.vertical:ScrollBar{}
                        delegate:ItemDelegate {
                            id:folderRow;required property var modelData;width:ListView.view.width;height:26
                            leftPadding:8+(modelData?modelData.split("/").length*10:0)
                            contentItem:Text{text:folderRow.modelData===null?"全部资产":folderRow.modelData===""?"根目录":folderRow.modelData.split("/").pop()
                                color:Theme.text;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter;font.pixelSize:12}
                            background:Rectangle{color:folderRow.modelData===root.folder?Theme.selectionHover:folderRow.hovered?Theme.hover:"transparent"}
                            onClicked:root.folder=modelData
                            ToolTip.visible:hovered && !!modelData;ToolTip.text:modelData||""
                        }
                    }
                }
            }
            Item {
                Layout.fillHeight:true;Layout.fillWidth:true
            GridView {
                boundsBehavior:Flickable.StopAtBounds
                id:grid;objectName:"assetShelfGrid";anchors.fill:parent;anchors.margins:8
                clip:true;cellWidth:150;cellHeight:142
                model:root.expanded?root.entries.filter(a=>(root.folder===null||a.folder===root.folder) && (!search.text||a.name.toLowerCase().includes(search.text.toLowerCase()))):[]
                ScrollBar.vertical:ScrollBar{}
                delegate:Rectangle {
                    id:card;required property var modelData;objectName:"libraryCard-"+modelData.id
                    width:140;height:132;radius:3;color:drag.hovered?Theme.hover:Theme.panel;border.color:drag.dragging?Theme.accent:Theme.border
                    Column {
                        anchors.fill:parent;anchors.margins:9;spacing:5
                        Rectangle {
                            width:parent.width;height:70;color:Theme.field;radius:2
                            Image {
                                anchors.fill:parent;fillMode:Image.PreserveAspectFit
                                source:editor.assetThumbnail(card.modelData.id,card.modelData.previewKey)
                            }
                        }
                        Text {text:card.modelData.name;width:parent.width;elide:Text.ElideRight;color:Theme.text;font.pixelSize:12}
                        Text {text:root.kindName(card.modelData.kind)+(card.modelData.collection==="graphs"?"":" · "+card.modelData.width+" × "+card.modelData.height+" × "+card.modelData.depth);width:parent.width;elide:Text.ElideRight;color:Theme.muted;font.pixelSize:10}
                    }
                    MouseArea {
                        id:drag;anchors.fill:parent;hoverEnabled:true;preventStealing:true
                        property point startPoint
                        property bool dragging:false
                        readonly property bool hovered:containsMouse
                        acceptedButtons:Qt.LeftButton|Qt.RightButton
                        cursorShape:dragging?Qt.DragCopyCursor:Qt.OpenHandCursor
                        onPressed:function(mouse){if(mouse.button===Qt.RightButton){assetMenu.assetId=card.modelData.id;assetMenu.collection=card.modelData.collection;assetMenu.popup();return;}root.dragCancelled=false;startPoint=Qt.point(mouse.x,mouse.y);dragging=false}
                        onPositionChanged:function(mouse){
                            if((pressedButtons&Qt.LeftButton)&&Math.hypot(mouse.x-startPoint.x,mouse.y-startPoint.y)>8){dragging=true;root.dragActive=true}
                            if(dragging&&!root.dragCancelled&&root.viewport){let p=mapToItem(root.viewport,mouse.x,mouse.y);root.viewport.previewAsset(card.modelData.id,p.x,p.y)}
                        }
                        onReleased:function(mouse){
                            const shouldDrop=dragging&&!root.dragCancelled&&root.viewport
                            const p=root.viewport?mapToItem(root.viewport,mouse.x,mouse.y):Qt.point(-1,-1)
                            const assetId=card.modelData.id
                            // Committing can replace this delegate. Release the
                            // library's Escape shortcut before changing models.
                            dragging=false;root.dragActive=false
                            if(shouldDrop){if(p.x>=0&&p.x<root.viewport.width&&p.y>=0&&p.y<root.viewport.height){root.viewport.forceActiveFocus();root.viewport.dropAsset(assetId,p.x,p.y)}else root.viewport.clearAssetPreview()}
                        }
                        onCanceled:{dragging=false;root.dragActive=false;if(root.viewport)root.viewport.clearAssetPreview()}
                        onDoubleClicked:root.editorRequested(card.modelData.id)
                    }
                    ToolTip.visible:drag.containsMouse&&!drag.pressed;ToolTip.delay:700;ToolTip.text:card.modelData.name+" · 双击编辑，拖入地表放置"
                }
            }
            Column {
                anchors.centerIn:parent;spacing:10;visible:grid.count===0
                Label {anchors.horizontalCenter:parent.horizontalCenter;text:search.text?"没有匹配的资产":root.folder!==null?"文件夹为空":"将结构、树木和洞穴收纳到资产库";color:Theme.secondary}
                WorkbenchButton {anchors.horizontalCenter:parent.horizontalCenter;visible:!search.text && root.folder===null;text:"导入第一个资产";onClicked:root.importRequested()}
            }
        }
    }
    }
}
