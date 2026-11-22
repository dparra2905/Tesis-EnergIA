import QtQuick
import "Tipografia.js" as Tipografia

// Campana del header: acceso global al centro de notificaciones con contador de no leídas.
Rectangle {
    id: root
    property int unreadCount: 0
    property bool active: false
    signal clicked()

    Accessible.role: Accessible.Button
    Accessible.name: "Notificaciones" + (root.unreadCount > 0 ? ", " + root.unreadCount + " sin leer" : "")

    width: 34; height: 34; radius: 8
    color: root.active || mouseArea.containsMouse ? "#161b22" : "transparent"
    border.color: root.active ? "#30363d" : "transparent"

    Icono {
        anchors.centerIn: parent
        nombre: "campana"
        tamano: 18
        color: root.active || mouseArea.containsMouse ? "#c9d1d9" : "#8b949e"
    }

    Rectangle {
        visible: root.unreadCount > 0
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: -3
        anchors.rightMargin: -3
        height: 16
        width: Math.max(16, badgeText.implicitWidth + 8)
        radius: 8
        color: "#f87171"
        border.color: "#0d1117"
        border.width: 2

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: root.unreadCount > 9 ? "9+" : root.unreadCount
            color: "#ffffff"
            font.pixelSize: Tipografia.detalle
            font.bold: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
