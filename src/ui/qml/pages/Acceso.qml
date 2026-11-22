import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/Tipografia.js" as Tipografia

// Inicio de sesión y registro (D-011). Se muestra antes de Main; la sesión (ClienteSupabase)
// la entrega main.cpp. Cuando sesion.puedeEntrar pasa a true, main.cpp abre Main.
Item {
    id: root
    property var sesion: null
    // "entrar" | "registrar" | "recuperar"
    property string modo: "entrar"
    // Recuperación de la contraseña: "pedir" (correo) | "codigo" (código + contraseña nueva)
    property string pasoRecuperar: "pedir"
    readonly property bool pideContrasenaNueva: root.modo === "registrar" || (root.modo === "recuperar" && root.pasoRecuperar === "codigo")

    readonly property bool verificando: root.sesion !== null && root.sesion.estado === "verificando"
    readonly property bool ocupado: root.sesion !== null && (root.sesion.ocupado || root.verificando)
    // Validación local del registro (la contraseña la valida también el servidor)
    readonly property string problemaRegistro: {
        if (!root.pideContrasenaNueva) return "";
        if (contrasena.text.length > 0 && contrasena.text.length < 6) return "La contraseña debe tener al menos 6 caracteres.";
        if (confirmacion.text.length > 0 && confirmacion.text !== contrasena.text) return "Las contraseñas no coinciden.";
        return "";
    }
    readonly property bool puedeEnviar: {
        if (root.ocupado || root.sesion === null || correo.text.trim() === "") return false;
        if (root.modo === "recuperar" && root.pasoRecuperar === "pedir") return true;
        if (contrasena.text === "") return false;
        if (root.modo === "entrar") return true;
        if (root.modo === "recuperar" && codigo.text.trim() === "") return false;
        return root.problemaRegistro === "" && confirmacion.text !== "";
    }

    function enviar() {
        if (!root.puedeEnviar) return;
        if (root.modo === "entrar") root.sesion.iniciarSesion(correo.text, contrasena.text);
        else if (root.modo === "registrar") root.sesion.registrar(correo.text, contrasena.text, nombre.text);
        else if (root.pasoRecuperar === "pedir") root.sesion.solicitarRecuperacion(correo.text);
        else root.sesion.restablecerContrasena(correo.text, codigo.text, contrasena.text);
    }
    function cambiarModo(m) {
        root.modo = m;
        root.pasoRecuperar = "pedir";
        confirmacion.text = "";
        contrasena.text = "";
        codigo.text = "";
        if (root.sesion) root.sesion.limpiarMensajes();
    }

    // Tras registrarse (con confirmación de correo) se vuelve a "Iniciar sesión" con el correo
    Connections {
        target: root.sesion
        ignoreUnknownSignals: true
        function onMensajeCambiado() {
            if (root.modo === "registrar" && root.sesion.aviso !== "") {
                root.modo = "entrar";
                contrasena.text = "";
                confirmacion.text = "";
            }
        }
        function onCodigoEnviado(c) { root.pasoRecuperar = "codigo"; codigo.forceActiveFocus(); }
    }

    Rectangle { anchors.fill: parent; color: "#0d1117" }

    Rectangle {
        id: tarjeta
        objectName: "tarjetaAcceso"
        anchors.centerIn: parent
        width: Math.min(420, root.width - 48)
        implicitHeight: columna.implicitHeight + 56
        height: implicitHeight
        radius: 14
        color: "#161b22"
        border.color: "#21262d"

        ColumnLayout {
            id: columna
            anchors.fill: parent
            anchors.margins: 28
            spacing: 14

            RowLayout {
                spacing: 10
                Rectangle {
                    width: 36; height: 36; radius: 9; color: "#22c55e"
                    Icono { anchors.centerIn: parent; nombre: "rayo"; tamano: 20; grosor: 2; color: "#0d1117" }
                }
                ColumnLayout {
                    spacing: 0
                    Text { text: "EnergIA"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloPagina; font.bold: true }
                    Text { text: "MONITOR"; color: "#22c55e"; font.pixelSize: Tipografia.detalle }
                }
            }

            Text {
                text: root.verificando ? "Verificando la sesión guardada…"
                    : root.modo === "entrar" ? "Inicia sesión para guardar tus corridas en la base de datos."
                    : root.modo === "registrar" ? "Crea una cuenta. Tus corridas son privadas hasta que decidas compartirlas."
                    : root.pasoRecuperar === "pedir" ? "Escribe el correo de tu cuenta: te enviaremos un código de 6 dígitos."
                    : "Escribe el código que te llegó y tu contraseña nueva."
                color: "#8b949e"; font.pixelSize: Tipografia.destacado; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }

            // Pestañas
            RowLayout {
                visible: !root.verificando && root.modo !== "recuperar"
                spacing: 6
                Layout.fillWidth: true
                Repeater {
                    model: [{ clave: "entrar", texto: "Iniciar sesión" }, { clave: "registrar", texto: "Crear cuenta" }]
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "pestana_" + modelData.clave
                        Layout.fillWidth: true
                        height: 40; radius: 8
                        color: root.modo === modelData.clave ? "#1f6feb33" : "transparent"
                        border.color: root.modo === modelData.clave ? "#3b82f6" : "#30363d"
                        Text {
                            anchors.centerIn: parent
                            text: modelData.texto
                            color: root.modo === modelData.clave ? "#e6edf3" : "#8b949e"
                            font.pixelSize: Tipografia.destacado; font.bold: root.modo === modelData.clave
                        }
                        MouseArea { anchors.fill: parent; enabled: !root.ocupado; onClicked: root.cambiarModo(modelData.clave) }
                    }
                }
            }

            ColumnLayout {
                visible: !root.verificando
                spacing: 6
                Layout.fillWidth: true

                Text { visible: root.modo === "registrar"; text: "Nombre (opcional)"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo }
                TextField {
                    id: nombre
                    objectName: "campoNombre"
                    visible: root.modo === "registrar"
                    Layout.fillWidth: true
                    placeholderText: "Como quieres que te vean los demás"
                    maximumLength: 80
                    selectByMouse: true
                    enabled: !root.ocupado
                    onAccepted: correo.forceActiveFocus()
                }
                Text { text: "Correo"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo }
                TextField {
                    id: correo
                    objectName: "campoCorreo"
                    Layout.fillWidth: true
                    placeholderText: "nombre@ejemplo.com"
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    selectByMouse: true
                    enabled: !root.ocupado && !(root.modo === "recuperar" && root.pasoRecuperar === "codigo")
                    focus: true
                    onAccepted: root.modo === "recuperar" ? root.enviar() : contrasena.forceActiveFocus()
                }
                Text {
                    visible: root.modo === "recuperar" && root.pasoRecuperar === "codigo"
                    text: "Código del correo"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                }
                TextField {
                    id: codigo
                    objectName: "campoCodigo"
                    visible: root.modo === "recuperar" && root.pasoRecuperar === "codigo"
                    Layout.fillWidth: true
                    placeholderText: "123456"
                    maximumLength: 10
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !root.ocupado
                    onAccepted: contrasena.forceActiveFocus()
                }
                Text {
                    visible: !(root.modo === "recuperar" && root.pasoRecuperar === "pedir")
                    text: root.modo === "recuperar" ? "Contraseña nueva" : "Contraseña"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                }
                TextField {
                    id: contrasena
                    objectName: "campoContrasena"
                    visible: !(root.modo === "recuperar" && root.pasoRecuperar === "pedir")
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    selectByMouse: true
                    enabled: !root.ocupado
                    onAccepted: root.pideContrasenaNueva ? confirmacion.forceActiveFocus() : root.enviar()
                }
                Text { visible: root.pideContrasenaNueva; text: "Repite la contraseña"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo }
                TextField {
                    id: confirmacion
                    objectName: "campoConfirmacion"
                    visible: root.pideContrasenaNueva
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    selectByMouse: true
                    enabled: !root.ocupado
                    onAccepted: root.enviar()
                }
            }

            Text {
                objectName: "textoError"
                visible: text !== ""
                text: root.problemaRegistro !== "" ? root.problemaRegistro : (root.sesion ? root.sesion.error : "")
                color: "#f85149"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                objectName: "textoAviso"
                visible: text !== ""
                text: root.sesion ? root.sesion.aviso : ""
                color: "#3fb950"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }

            Button {
                objectName: "botonEnviar"
                visible: !root.verificando
                Layout.fillWidth: true
                text: root.ocupado ? "Un momento…"
                    : root.modo === "entrar" ? "Iniciar sesión"
                    : root.modo === "registrar" ? "Crear cuenta"
                    : root.pasoRecuperar === "pedir" ? "Enviar código" : "Cambiar contraseña"
                enabled: root.puedeEnviar
                onClicked: root.enviar()
            }

            RowLayout {
                visible: !root.verificando && root.modo !== "registrar"
                Layout.fillWidth: true
                Enlace {
                    objectName: "enlaceRecuperar"
                    text: root.modo === "recuperar" ? "Volver a iniciar sesión" : "¿Olvidaste tu contraseña?"
                    icono: root.modo === "recuperar" ? "atras" : ""
                    enabled: !root.ocupado
                    onClicked: root.cambiarModo(root.modo === "recuperar" ? "entrar" : "recuperar")
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: root.modo === "recuperar" && root.pasoRecuperar === "codigo"
                    text: "Pedir otro código"
                    color: "#3b82f6"; font.pixelSize: Tipografia.cuerpo
                    MouseArea { anchors.fill: parent; enabled: !root.ocupado; onClicked: root.pasoRecuperar = "pedir" }
                }
            }

            BusyIndicator {
                visible: root.verificando
                running: visible
                Layout.alignment: Qt.AlignHCenter
            }

            // Sin red: la medición no depende de la base (D-011)
            ColumnLayout {
                visible: root.sesion !== null && root.sesion.errorDeRed && !root.ocupado
                spacing: 6
                Layout.fillWidth: true
                Rectangle { Layout.fillWidth: true; height: 1; color: "#21262d" }
                Text {
                    text: "Puedes medir sin conexión: las corridas quedan pendientes y se suben cuando inicies sesión en este equipo."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Button {
                    objectName: "botonSinConexion"
                    text: "Continuar sin conexión"
                    Layout.fillWidth: true
                    onClicked: root.sesion.continuarSinConexion()
                }
            }
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 16
        text: !root.sesion ? "Vista sin backend: no hay sesión."
                           : "Las corridas se guardan siempre en este equipo; la base de datos guarda una copia."
        color: "#484f58"; font.pixelSize: Tipografia.auxiliar
    }
}
