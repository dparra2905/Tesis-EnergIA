import QtQuick
import QtQuick.Shapes
import "Iconos.js" as Iconos

// Icono vectorial del juego de Iconos.js: se dibuja con trazos, toma cualquier color y se ve
// nítido a cualquier tamaño. Es decorativo: el texto que lo acompaña (o Accessible.name del
// control que lo contiene) es el que da el significado.
Item {
    id: root
    property string nombre: ""
    property color color: "#8b949e"
    // Lado en píxeles; el grosor del trazo se mantiene en píxeles al cambiar el tamaño
    property real tamano: 16
    property real grosor: tamano <= 16 ? 1.5 : 1.75

    readonly property string trazo: Iconos.trazo(nombre)

    implicitWidth: tamano
    implicitHeight: tamano
    Accessible.ignored: true

    Shape {
        width: 24
        height: 24
        visible: root.trazo !== ""
        preferredRendererType: Shape.CurveRenderer
        transform: Scale { xScale: root.tamano / 24; yScale: root.tamano / 24 }

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.grosor * 24 / Math.max(1, root.tamano)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.trazo }
        }
    }
}
