import QtQuick
import QtQuick.Controls

// Botón estándar con un icono de Iconos.js delante del texto. Conserva el fondo y la paleta del
// estilo (Fusion); `text` sigue siendo el nombre accesible del botón.
Button {
    id: control
    property string icono: ""
    property color colorContenido: control.palette.buttonText

    contentItem: Item {
        implicitWidth: fila.implicitWidth
        implicitHeight: fila.implicitHeight

        Row {
            id: fila
            anchors.centerIn: parent
            spacing: 8

            Icono {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.icono !== ""
                nombre: control.icono
                tamano: 14
                color: control.colorContenido
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.colorContenido
            }
        }
    }
}
