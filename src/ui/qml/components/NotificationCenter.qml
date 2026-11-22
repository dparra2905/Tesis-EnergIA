import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import "Tipografia.js" as Tipografia

// Panel persistente de notificaciones (estilo historial de descargas del navegador).
// Cerrarlo no borra nada: las notificaciones permanecen en appState.notifications.
Popup {
    id: root
    property AppState appState

    width: 410
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: -2
            anchors.rightMargin: -2
            anchors.bottomMargin: -6
            radius: 14
            color: "#000000"
            opacity: 0.35
        }
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#161b22"
            border.color: "#30363d"
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Notificaciones"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloSeccion; font.bold: true; Layout.fillWidth: true }
                Text {
                    Layout.fillWidth: true
                    text: root.appState.unreadCount > 0
                          ? root.appState.unreadCount + " sin leer"
                          : "Todo al día"
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                }
            }

            Text {
                readonly property bool available: root.appState.unreadCount > 0
                text: "Marcar todas como leídas"
                color: !available ? "#484f58" : (markAllArea.containsMouse ? "#4ade80" : "#22c55e")
                font.pixelSize: Tipografia.cuerpo
                MouseArea {
                    id: markAllArea
                    anchors.fill: parent
                    enabled: parent.available
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.appState.markAllRead()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 380)
            visible: count > 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.appState.notifications
            readonly property bool scrollable: contentHeight > height
            ScrollBar.vertical: T.ScrollBar {
                id: scrollBar
                policy: list.scrollable ? T.ScrollBar.AlwaysOn : T.ScrollBar.AsNeeded
                padding: 2
                implicitWidth: 10
                background: Rectangle { color: "transparent" }
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: scrollBar.pressed ? "#6e7681" : (scrollBar.hovered ? "#484f58" : "#30363d")
                }
            }

            delegate: Column {
                width: ListView.view.width - (list.scrollable ? 8 : 0)

                NotificationItem {
                    width: parent.width
                    kind: model.kind
                    title: model.title
                    description: model.description
                    time: model.time
                    read: model.read
                    decision: model.dir !== undefined && model.dir !== ""
                    resuelta: model.resuelta === true
                    corridaActiva: root.appState.corridaActiva
                    avisoCorridaActiva: root.appState.avisoCorridaActiva
                    onMarkReadRequested: root.appState.markRead(index)
                    onDecisionRequested: (accion) => root.appState.decidir(index, accion)
                }
                Rectangle { width: parent.width; height: 1; color: "#21262d"; visible: index < list.count - 1 }
            }
        }

        Text {
            visible: list.count === 0
            Layout.fillWidth: true
            Layout.margins: 24
            horizontalAlignment: Text.AlignHCenter
            text: "No hay notificaciones"
            color: "#8b949e"
            font.pixelSize: Tipografia.cuerpo
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }

        Text {
            Layout.fillWidth: true
            Layout.margins: 10
            horizontalAlignment: Text.AlignHCenter
            text: "Las notificaciones se conservan en este panel"
            color: "#484f58"
            font.pixelSize: Tipografia.detalle
        }
    }
}
