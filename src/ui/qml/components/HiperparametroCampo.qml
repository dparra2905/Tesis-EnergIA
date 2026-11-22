import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Campo de un hiperparámetro generado desde la descripción que entrega la DLL (D-009).
// 'descriptor' es un elemento de "hiperparametros" del catálogo:
//   { clave, etiqueta, ayuda, tipo: entero | real | opcion | lista_enteros,
//     minimo, maximo, por_defecto, opciones, max_elementos }
// Expone 'valor' (listo para config.json) y 'valido'.
ColumnLayout {
    id: root

    property var descriptor: ({})
    readonly property string tipo: descriptor && descriptor.tipo ? descriptor.tipo : ""
    readonly property string clave: descriptor && descriptor.clave ? descriptor.clave : ""

    // Valor para config.json: número (entero/real), nombre de la opción o texto de la lista
    readonly property var valor: {
        if (tipo === "opcion") return opciones.currentIndex >= 0 ? descriptor.opciones[opciones.currentIndex] : "";
        var t = campo.text.trim();
        if (tipo === "entero") return parseInt(t, 10);
        if (tipo === "real") return parseFloat(t);
        return t;
    }
    readonly property bool valido: {
        if (tipo === "opcion") return opciones.currentIndex >= 0;
        var t = campo.text.trim();
        if (tipo === "entero") return /^-?\d+$/.test(t) && valor >= descriptor.minimo && valor <= descriptor.maximo;
        // Decimal o notación científica: String() escribe 1e-7 (ganancia mínima del Árbol) así
        if (tipo === "real") return /^-?\d+(\.\d+)?([eE][-+]?\d+)?$/.test(t) && valor >= descriptor.minimo && valor <= descriptor.maximo;
        if (tipo === "lista_enteros") {
            if (!/^\d+(,\d+)*$/.test(t)) return false;
            var partes = t.split(",");
            if (partes.length > descriptor.max_elementos) return false;
            for (var i = 0; i < partes.length; i++) {
                var n = parseInt(partes[i], 10);
                if (n < descriptor.minimo || n > descriptor.maximo) return false;
            }
            return true;
        }
        return false;
    }

    function rango() {
        if (tipo === "entero" || tipo === "real") return descriptor.minimo + " – " + descriptor.maximo;
        if (tipo === "lista_enteros")
            return "hasta " + descriptor.max_elementos + " enteros de " + descriptor.minimo + " a " + descriptor.maximo + ", separados por comas";
        return "";
    }

    function restablecer() {
        if (tipo === "opcion") opciones.currentIndex = descriptor.por_defecto;
        else campo.text = descriptor.por_defecto !== undefined ? String(descriptor.por_defecto) : "";
    }

    spacing: 4
    Component.onCompleted: restablecer()
    onDescriptorChanged: restablecer()

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text { text: descriptor.etiqueta || root.clave; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
        Text { text: root.rango(); color: "#484f58"; font.pixelSize: Tipografia.detalle; visible: text !== ""; Layout.fillWidth: true; elide: Text.ElideRight }
    }

    TextField {
        id: campo
        objectName: "campo_" + root.clave
        visible: root.tipo !== "opcion"
        Layout.fillWidth: true
        implicitHeight: 40
        color: "#e6edf3"
        font.pixelSize: Tipografia.destacado
        selectByMouse: true
        inputMethodHints: root.tipo === "lista_enteros" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
        background: Rectangle {
            radius: 8
            color: "#0d1117"
            border.color: root.valido ? "#30363d" : "#f87171"
        }
    }

    ComboBox {
        id: opciones
        objectName: "opciones_" + root.clave
        visible: root.tipo === "opcion"
        Layout.fillWidth: true
        implicitHeight: 40
        model: root.tipo === "opcion" ? root.descriptor.opciones : []
    }

    Text {
        text: descriptor.ayuda || ""
        visible: text !== ""
        color: "#8b949e"
        font.pixelSize: Tipografia.auxiliar
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Text {
        text: "Valor fuera del rango permitido"
        visible: !root.valido && root.tipo !== "opcion"
        color: "#f87171"
        font.pixelSize: Tipografia.auxiliar
    }
}
