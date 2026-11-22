import QtQuick
import QtQuick.Controls

// Ayuda breve junto a una métrica o un título: un icono de interrogación que muestra el texto
// al pasar el ratón o al llegar con el teclado (Tab).
Item {
    id: root
    property string texto: ""
    property real tamano: 13

    implicitWidth: tamano
    implicitHeight: tamano
    activeFocusOnTab: true
    Accessible.role: Accessible.HelpBalloon
    Accessible.name: texto

    Icono {
        anchors.centerIn: parent
        nombre: "ayuda"
        tamano: root.tamano
        color: area.containsMouse || root.activeFocus ? "#c9d1d9" : "#6e7681"
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.WhatsThisCursor
    }

    ToolTip {
        objectName: "textoAyuda"
        parent: root
        visible: root.texto !== "" && (area.containsMouse || root.activeFocus)
        delay: 300
        text: root.texto
        width: Math.min(implicitWidth, 320)
    }
}
