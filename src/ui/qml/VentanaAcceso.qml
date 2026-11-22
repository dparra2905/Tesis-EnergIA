import QtQuick
import QtQuick.Controls
import "components"
import "pages"
import "components/Tipografia.js" as Tipografia

// Ventana de inicio de sesión (D-011): main.cpp la abre antes de Main y la oculta cuando
// la sesión permite entrar. Vuelve a mostrarse al cerrar sesión.
ApplicationWindow {
    id: window
    width: 1440
    height: 860
    minimumWidth: 1150
    minimumHeight: 720
    visible: true
    title: "EnergIA Monitor — Iniciar sesión"
    color: "#0d1117"

    // Paleta oscura comun (components/PaletaOscura.qml): un solo lugar para todos los controles
    palette: PaletaOscura {}
    font.pixelSize: Tipografia.cuerpo

    // ClienteSupabase (C++), entregado por main.cpp
    property var sesion: null

    Acceso {
        anchors.fill: parent
        sesion: window.sesion
    }
}
