import QtQuick
import "Tipografia.js" as Tipografia

// Pastilla compacta que indica qué monitor alimenta una sección y su estado.
Rectangle {
    id: root
    property string text: ""
    property color statusColor: "#8b949e"

    implicitWidth: row.implicitWidth + 16
    implicitHeight: Math.max(27, row.implicitHeight + 10)
    radius: implicitHeight / 2
    color: "#0d1117"
    border.color: "#30363d"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            width: 6; height: 6; radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: root.statusColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: "#c9d1d9"
            font.pixelSize: Tipografia.auxiliar
        }
    }
}
