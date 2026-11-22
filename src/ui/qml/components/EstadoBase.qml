import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Marca de una corrida local frente a la base de datos (D-011): punto + texto corto.
// 'estado' es lo que devuelve ColaSubidas.estadoDe(dir); sin cola (sin backend) no se muestra.
RowLayout {
    id: root
    property var estado: null

    readonly property var info: {
        var e = root.estado;
        if (!e) return { texto: "", color: "#8b949e" };
        if (e.estado === "subida") return { texto: "En la base", color: "#3fb950" };
        if (e.propietario === "otra") return { texto: "De otra cuenta", color: "#8b949e" };
        if (e.propietario === "sin_cuenta") return { texto: "Sin cuenta", color: "#f59e0b" };
        switch (e.estado) {
        case "esperando_decision": return { texto: "Esperando decisión", color: "#f59e0b" };
        case "pendiente": return { texto: "Pendiente", color: "#f59e0b" };
        case "subiendo": return { texto: "Subiendo…", color: "#3b82f6" };
        case "rechazada": return { texto: "Rechazada", color: "#f85149" };
        default: return { texto: "Solo local", color: "#8b949e" };
        }
    }

    visible: root.estado !== null
    spacing: 5
    Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: root.info.color }
    Text { objectName: "textoEstadoBase"; text: root.info.texto; color: root.info.color; font.pixelSize: Tipografia.auxiliar }
}
