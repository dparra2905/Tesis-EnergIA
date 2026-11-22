import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Lectura de una métrica: valor actual con su unidad y, si hay historial, una sparkline.
Rectangle {
    id: root
    // Nombre del icono (Iconos.js) y ayuda breve sobre la métrica y su unidad (opcional)
    property string icon: ""
    property string ayuda: ""
    property string label: ""
    property string value: ""
    property string unit: ""
    property color tint: "#22c55e"
    property var values: []

    radius: 10
    color: "#0d1117"
    border.color: "#30363d"
    implicitHeight: Math.max(136, cardCol.implicitHeight + 24)
    Layout.fillHeight: true

    ColumnLayout {
        id: cardCol
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 12
        spacing: 6

        RowLayout {
            objectName: "cabeceraMetricaViva"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            spacing: 6
            Icono { visible: root.icon !== ""; nombre: root.icon; tamano: 14; color: root.tint; Layout.alignment: Qt.AlignVCenter }
            Rectangle { visible: root.icon === ""; Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: root.tint; Layout.alignment: Qt.AlignVCenter }
            Text {
                text: root.label; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                Layout.fillWidth: true; Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
            }
            Ayuda { visible: root.ayuda !== ""; texto: root.ayuda; Layout.alignment: Qt.AlignVCenter }
        }
        Text {
            text: root.value + (root.unit ? " " + root.unit : "")
            color: "#e6edf3"; font.pixelSize: Tipografia.marca; font.bold: true
            Layout.fillWidth: true; wrapMode: Text.WordWrap
        }
        Sparkline {
            values: root.values
            lineColor: root.tint
            Layout.fillWidth: true
            visible: root.values.length > 0
        }
    }
}
