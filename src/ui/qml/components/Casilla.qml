import QtQuick
import QtQuick.Controls

// Casilla de verificación de la aplicación. Es un CheckBox con el texto de Fusion y su propio
// cuadro: Fusion lo dibuja con palette.base y un borde derivado de palette.window, de modo que
// con PaletaOscura una casilla sin marcar casi no se veía sobre las tarjetas (#161b22). Aquí el
// cuadro tiene un borde gris claro (sin marcar) y se rellena de azul con un visto (marcada).
CheckBox {
    id: control

    // Colores del cuadro (expuestos para las pruebas)
    readonly property color colorBorde: !enabled ? "#484f58"
                                        : checked ? "#3b82f6"
                                        : (hovered || visualFocus) ? "#c9d1d9" : "#8b949e"
    readonly property color colorRelleno: checked ? (enabled ? "#3b82f6" : "#30363d")
                                          : down ? "#21262d" : "#0d1117"

    indicator: Rectangle {
        objectName: "cuadroCasilla"
        implicitWidth: 16
        implicitHeight: 16
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: control.colorRelleno
        border.width: 1.5
        border.color: control.colorBorde

        Icono {
            anchors.centerIn: parent
            visible: control.checked
            nombre: "check"
            tamano: 12
            grosor: 2
            color: control.enabled ? "#ffffff" : "#8b949e"
        }
    }
}
