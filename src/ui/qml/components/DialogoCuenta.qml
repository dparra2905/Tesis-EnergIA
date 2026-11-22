import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Mi cuenta (D-011): nombre visible para los demás y cambio de contraseña.
// 'sesion' es ClienteSupabase; el resultado llega por cuentaActualizada(ok, mensaje).
Popup {
    id: root
    property var sesion: null
    property string mensaje: ""
    property bool mensajeOk: true
    property bool esperando: false

    modal: true
    focus: true
    width: 380
    padding: 20
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: "#161b22"; border.color: "#30363d"; radius: 12 }

    onOpened: {
        nombre.text = root.sesion ? root.sesion.nombre : "";
        actual.text = ""; nueva.text = ""; repetida.text = "";
        root.mensaje = "";
        root.esperando = false;
    }

    Connections {
        target: root.sesion
        ignoreUnknownSignals: true
        function onCuentaActualizada(ok, mensaje) {
            root.esperando = false;
            root.mensajeOk = ok;
            root.mensaje = mensaje;
            if (ok) { actual.text = ""; nueva.text = ""; repetida.text = ""; }
        }
    }

    readonly property string problemaContrasena: {
        if (nueva.text.length > 0 && nueva.text.length < 6) return "La contraseña nueva debe tener al menos 6 caracteres.";
        if (repetida.text.length > 0 && repetida.text !== nueva.text) return "Las contraseñas no coinciden.";
        return "";
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text { text: "Mi cuenta"; color: "#e6edf3"; font.pixelSize: Tipografia.cifra; font.bold: true }
        Text {
            text: root.sesion ? root.sesion.correo : ""
            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
        }

        Text { text: "Nombre visible para los demás usuarios"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: nombre
                objectName: "campoNombreCuenta"
                Layout.fillWidth: true
                maximumLength: 80
                selectByMouse: true
            }
            Button {
                objectName: "botonGuardarNombre"
                text: "Guardar"
                enabled: !root.esperando && root.sesion !== null && root.sesion.conectado
                         && nombre.text.trim() !== "" && nombre.text.trim() !== root.sesion.nombre
                onClicked: { root.esperando = true; root.sesion.cambiarNombre(nombre.text); }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#21262d" }

        Text { text: "Cambiar la contraseña"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
        TextField { id: actual; objectName: "campoContrasenaActual"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Contraseña actual" }
        TextField { id: nueva; objectName: "campoContrasenaNueva"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Contraseña nueva" }
        TextField { id: repetida; objectName: "campoContrasenaRepetida"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Repite la contraseña nueva" }
        Button {
            objectName: "botonCambiarContrasena"
            Layout.fillWidth: true
            text: "Cambiar contraseña"
            enabled: !root.esperando && root.sesion !== null && root.sesion.conectado && actual.text !== ""
                     && nueva.text !== "" && repetida.text !== "" && root.problemaContrasena === ""
            onClicked: { root.esperando = true; root.sesion.cambiarContrasena(actual.text, nueva.text); }
        }

        Text {
            objectName: "mensajeCuenta"
            visible: text !== ""
            text: root.problemaContrasena !== "" ? root.problemaContrasena
                : root.esperando ? "Un momento…"
                : root.sesion && !root.sesion.conectado ? "Hace falta conexión con la base de datos."
                : root.mensaje
            color: root.problemaContrasena !== "" || !root.mensajeOk ? "#f85149" : "#3fb950"
            font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        Button { text: "Cerrar"; Layout.alignment: Qt.AlignRight; onClicked: root.close() }
    }
}
