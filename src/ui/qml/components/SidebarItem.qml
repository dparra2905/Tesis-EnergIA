import QtQuick
import "Tipografia.js" as Tipografia

Rectangle {
    id: root
    property string label: ""
    // Nombre del icono (Iconos.js)
    property string icon: ""
    property bool active: false
    signal clicked()

    height: Math.max(44, labelText.implicitHeight + 18)
    radius: 8
    Accessible.role: Accessible.Button
    Accessible.name: label
    color: active ? "#16311f" : (mouseArea.containsMouse ? "#161b22" : "transparent")

    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        spacing: 10

        Icono {
            anchors.verticalCenter: parent.verticalCenter
            nombre: root.icon
            tamano: 16
            color: root.active ? "#22c55e" : "#8b949e"
        }
        Text {
            id: labelText
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.active ? "#22c55e" : "#c9d1d9"
            font.pixelSize: Tipografia.subtitulo
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: root.clicked()
    }
}
