import QtQuick
import "Tipografia.js" as Tipografia

Rectangle {
    id: root
    property string label: ""
    property bool checked: true

    implicitWidth: Math.max(94, row.implicitWidth + 24)
    implicitHeight: Math.max(35, row.implicitHeight + 12)
    radius: 8
    color: checked ? "#16311f" : "#0d1117"
    border.color: checked ? "#22c55e" : "#30363d"
    border.width: 1

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            width: 14; height: 14; radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? "#22c55e" : "transparent"
            border.color: root.checked ? "#22c55e" : "#484f58"
            border.width: 1

            Icono {
                anchors.centerIn: parent
                visible: root.checked
                nombre: "check"
                tamano: 11
                grosor: 2
                color: "#0d1117"
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.checked ? "#e6edf3" : "#8b949e"
            font.pixelSize: Tipografia.destacado
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.checked = !root.checked
    }
}
