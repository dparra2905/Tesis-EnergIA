import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

RowLayout {
    property string time: ""
    property string message: ""
    property color dotColor: "#22c55e"

    spacing: 10
    Layout.fillWidth: true

    Rectangle { width: 6; height: 6; radius: 3; color: dotColor; Layout.alignment: Qt.AlignVCenter }
    Text { text: time; color: "#484f58"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 45 }
    Text { text: message; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true }
}
