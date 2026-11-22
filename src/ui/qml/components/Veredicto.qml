import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Veredicto de la auditoría de una corrida (D-012): punto + texto corto. La auditoría informa;
// no bloquea la corrida ni la subida. "" = la corrida no tiene auditoría (corrida vieja).
RowLayout {
    id: root
    property string veredicto: ""

    readonly property var info: ({
        valida: { texto: "Válida", color: "#3fb950" },
        con_advertencias: { texto: "Con advertencias", color: "#f59e0b" },
        no_valida: { texto: "No válida", color: "#f85149" }
    })[root.veredicto] || { texto: "Sin auditoría", color: "#8b949e" }

    spacing: 5
    Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: root.info.color }
    Text { objectName: "textoVeredicto"; text: root.info.texto; color: root.info.color; font.pixelSize: Tipografia.auxiliar }
}
