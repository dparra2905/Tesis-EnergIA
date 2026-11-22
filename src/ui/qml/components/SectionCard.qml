import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Tarjeta estándar: título, subtítulo y acento opcionales, insignia de monitor
// a la derecha y el contenido apilado en una columna.
Rectangle {
    id: root
    property string title: ""
    property string subtitle: ""
    property color accent: "transparent"
    property string badgeText: ""
    property color badgeColor: "#8b949e"
    // Ayuda breve junto al título (opcional)
    property string ayuda: ""
    default property alias content: body.data

    implicitHeight: layout.implicitHeight + 32
    radius: 12
    color: "#161b22"
    border.color: "#21262d"

    ColumnLayout {
        id: layout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                visible: root.accent.a > 0
                Layout.preferredWidth: 3
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                radius: 2
                color: root.accent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: root.title; color: "#e6edf3"
                        font.pixelSize: Tipografia.subtitulo; font.bold: true
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Ayuda { visible: root.ayuda !== ""; texto: root.ayuda; Layout.alignment: Qt.AlignVCenter }
                }
                Text {
                    visible: root.subtitle !== ""
                    text: root.subtitle
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

            MonitorBadge {
                visible: root.badgeText !== ""
                Layout.alignment: Qt.AlignTop
                text: root.badgeText
                statusColor: root.badgeColor
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 12
        }
    }
}
