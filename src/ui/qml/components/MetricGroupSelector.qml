import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Bloque de selección de métricas de un grupo (recursos o energía) junto al monitor que lo alimenta.
Rectangle {
    id: root
    property string title: ""
    property string monitorName: ""
    property color accent: "#3b82f6"
    property var metrics: []          // [{ key, label, color }]
    property bool showToggle: false
    property bool included: true
    signal toggled(bool included)

    implicitHeight: col.implicitHeight + 28
    radius: 10
    color: "#0d1117"
    border.color: "#30363d"

    ColumnLayout {
        id: col
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 26; radius: 2; color: root.accent }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { text: root.title; color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: root.monitorName; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }

            ToggleSwitch {
                visible: root.showToggle
                checked: root.included
                onToggled: (checked) => root.toggled(checked)
            }
        }

        Flow {
            objectName: "flujoMetricas"
            Layout.fillWidth: true
            // El Flow debe comunicar a ColumnLayout la altura de sus filas al envolver CPU/RAM.
            Layout.preferredHeight: childrenRect.height
            spacing: 8
            opacity: root.included ? 1 : 0.4
            enabled: root.included

            Repeater {
                model: root.metrics
                delegate: MetricChip { label: modelData.label; checked: true }
            }
        }
    }
}
