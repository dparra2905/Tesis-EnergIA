import QtQuick
import "Tipografia.js" as Tipografia

// Enlace de texto, con un icono opcional de Iconos.js delante (por ejemplo "atras").
Item {
    id: root
    property string text: ""
    property string icono: ""
    property color color: "#3b82f6"
    property int tamanoLetra: Tipografia.cuerpo
    signal clicked()

    implicitWidth: fila.implicitWidth
    implicitHeight: fila.implicitHeight
    Accessible.role: Accessible.Link
    Accessible.name: text

    Row {
        id: fila
        spacing: 4

        Icono {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icono !== ""
            nombre: root.icono
            tamano: root.tamanoLetra + 3
            color: root.color
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.color
            font.pixelSize: root.tamanoLetra
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
