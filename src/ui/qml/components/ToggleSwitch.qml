import QtQuick

Rectangle {
    id: root
    property bool checked: false
    signal toggled(bool checked)

    width: 40; height: 22; radius: 11
    color: checked ? "#22c55e" : "#30363d"
    border.color: checked ? "#22c55e" : "#484f58"

    Rectangle {
        width: 16; height: 16; radius: 8
        color: "#e6edf3"
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? root.width - width - 3 : 3
        Behavior on x { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.checked = !root.checked; root.toggled(root.checked); }
    }
}
