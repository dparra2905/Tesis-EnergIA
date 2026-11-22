import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Fila de un monitor: nombre, qué mide, estado actual y, opcionalmente, interruptor.
RowLayout {
    id: root
    property string name: ""
    property string detail: ""
    property string status: ""
    property color statusColor: "#8b949e"
    property bool showToggle: false
    property bool checked: false
    signal toggled(bool checked)

    spacing: 10

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text { text: root.name; color: "#e6edf3"; font.pixelSize: Tipografia.destacado; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Text { text: root.detail; color: "#8b949e"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        MonitorBadge { text: root.status; statusColor: root.statusColor }
    }

    ToggleSwitch {
        visible: root.showToggle
        Layout.alignment: Qt.AlignTop
        checked: root.checked
        onToggled: (checked) => root.toggled(checked)
    }
}
