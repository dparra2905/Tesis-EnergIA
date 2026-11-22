import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

Rectangle {
    id: card
    property string title: ""
    property string value: ""
    property string caption: ""
    property color dotColor: "transparent"
    // Ayuda breve sobre la métrica y su unidad (opcional)
    property string ayuda: ""

    implicitHeight: Math.max(105, content.implicitHeight + 36)
    Layout.preferredHeight: implicitHeight
    Layout.fillHeight: true
    radius: 12
    color: "#161b22"
    border.color: "#21262d"

    ColumnLayout {
        id: content
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 8

        RowLayout {
            objectName: "cabeceraMetrica"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            spacing: 6
            Text {
                text: card.title
                color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                Layout.fillWidth: true; Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
            Ayuda { visible: card.ayuda !== ""; texto: card.ayuda; Layout.alignment: Qt.AlignVCenter }
        }

        RowLayout {
            objectName: "valorMetrica"
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                visible: dotColor != "transparent"
                Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3
                Layout.alignment: Qt.AlignVCenter
                color: dotColor
            }
            Text {
                text: value; color: "#e6edf3"; font.pixelSize: Tipografia.cifra; font.bold: true
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }

        Text {
            visible: caption !== ""
            text: caption
            color: "#484f58"; font.pixelSize: Tipografia.detalle
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
