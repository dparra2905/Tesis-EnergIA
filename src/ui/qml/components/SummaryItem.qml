import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

RowLayout {
    // Nombre del icono (Iconos.js)
    property string icon: ""
    property string title: ""
    property string description: ""

    spacing: 10
    Layout.fillWidth: true

    Icono { nombre: icon; tamano: 16; color: "#8b949e"; Layout.alignment: Qt.AlignTop }

    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        Text { text: title; color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true }
        Text {
            text: description
            color: "#8b949e"
            font.pixelSize: Tipografia.cuerpo
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Icono { nombre: "check"; tamano: 14; grosor: 2; color: "#22c55e"; Layout.alignment: Qt.AlignTop }
}
