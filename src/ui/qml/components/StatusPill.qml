import QtQuick
import QtQuick.Controls
import "Tipografia.js" as Tipografia

// Estado de un monitor en el header: icono, nombre, qué mide y estado actual.
Rectangle {
    id: root
    // Nombre del icono (Iconos.js)
    property string icon: ""
    property string label: ""
    property string hint: ""
    property string status: ""
    // En ventanas estrechas se conservan icono y estado; la etiqueta completa queda en la ayuda.
    property bool compact: false
    property color dotColor: "#8b949e"

    implicitWidth: row.implicitWidth + 20
    implicitHeight: Math.max(44, row.implicitHeight + 12)
    radius: 8
    color: "#161b22"
    border.color: "#21262d"
    ToolTip.visible: hoverHandler.hovered && root.compact
    ToolTip.text: root.label + " · " + root.hint + " · " + root.status
    HoverHandler { id: hoverHandler }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10

        Icono {
            anchors.verticalCenter: parent.verticalCenter
            nombre: root.icon
            tamano: 16
            color: "#8b949e"
        }

        Column {
            visible: !root.compact
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text { text: root.label; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo }
            Text { text: root.hint; color: "#8b949e"; font.pixelSize: Tipografia.detalle; visible: root.hint !== "" }
        }

        Rectangle { visible: !root.compact; width: 1; height: 20; color: "#30363d"; anchors.verticalCenter: parent.verticalCenter }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: root.dotColor
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.status
                color: "#c9d1d9"
                font.pixelSize: Tipografia.cuerpo
            }
        }
    }
}
