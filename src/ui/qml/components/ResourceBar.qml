import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

ColumnLayout {
    property string label: ""
    property int percent: 0
    property color barColor: "#3b82f6"

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        Text { text: label; color: "#c9d1d9"; font.pixelSize: Tipografia.destacado }
        Item { Layout.fillWidth: true }
        Text { text: percent + "%"; color: barColor; font.pixelSize: Tipografia.destacado; font.bold: true }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 6
        radius: 3
        color: "#21262d"

        Rectangle {
            width: parent.width * (percent / 100)
            height: parent.height
            radius: 3
            color: barColor
        }
    }
}
