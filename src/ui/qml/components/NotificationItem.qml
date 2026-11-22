import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Fila del centro de notificaciones.
Rectangle {
    id: root
    property string kind: "info"      // success | info | warning | error
    property string title: ""
    property string description: ""
    property string time: ""
    property bool read: true
    // Corrida que espera decisión (modo de subida, D-012): la notificación lleva las acciones
    property bool decision: false
    property bool resuelta: false
    // Con una corrida activa, Subir y Subir con nota esperan (AppState.corridaActiva)
    property bool corridaActiva: false
    property string avisoCorridaActiva: ""
    signal markReadRequested()
    signal decisionRequested(string accion)

    readonly property color kindColor: ({ success: "#22c55e", info: "#3b82f6", warning: "#f59e0b", error: "#f87171" })[kind] || "#8b949e"
    readonly property string kindIcon: ({ success: "check", info: "letra_i", warning: "exclamacion", error: "cerrar" })[kind] || "letra_i"

    implicitHeight: row.implicitHeight + 24
    color: rowArea.containsMouse ? "#1c2330" : (root.read ? "transparent" : "#111a26")

    MouseArea {
        id: rowArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.read ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked: if (!root.read) root.markReadRequested()
    }

    RowLayout {
        id: row
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 12
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignTop
            radius: 14
            color: Qt.rgba(root.kindColor.r, root.kindColor.g, root.kindColor.b, 0.15)
            Icono { anchors.centerIn: parent; nombre: root.kindIcon; tamano: 14; grosor: 2; color: root.kindColor }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: root.title
                    color: root.read ? "#c9d1d9" : "#e6edf3"
                    font.pixelSize: Tipografia.destacado
                    font.bold: !root.read
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text { text: root.time; color: "#484f58"; font.pixelSize: Tipografia.detalle }
            }

            Text {
                text: root.description
                color: "#8b949e"
                font.pixelSize: Tipografia.cuerpo
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                objectName: "accionesDecision"
                visible: root.decision && !root.resuelta
                spacing: 6
                Repeater {
                    model: [{ accion: "subir", texto: "Subir" }, { accion: "subir_con_nota", texto: "Subir con nota" },
                            { accion: "no_subir", texto: "No subir" }]
                    delegate: Button {
                        required property var modelData
                        objectName: "accion_" + modelData.accion
                        text: modelData.texto
                        font.pixelSize: Tipografia.auxiliar
                        implicitHeight: 32
                        enabled: modelData.accion === "no_subir" || !root.corridaActiva
                        onClicked: root.decisionRequested(modelData.accion)
                    }
                }
            }
            Text {
                visible: root.decision && !root.resuelta && root.corridaActiva
                text: root.avisoCorridaActiva
                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
            }
            Text {
                visible: root.decision && root.resuelta
                text: "Decisión tomada"
                color: "#484f58"; font.pixelSize: Tipografia.auxiliar
            }

            Text {
                visible: !root.read
                text: "Marcar como leída"
                color: markArea.containsMouse ? "#4ade80" : "#22c55e"
                font.pixelSize: Tipografia.auxiliar
                MouseArea {
                    id: markArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.markReadRequested()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            radius: 4
            color: "#22c55e"
            opacity: root.read ? 0 : 1
        }
    }
}
