import QtQuick

// Paleta oscura de la aplicacion, en un solo lugar: Main.qml y VentanaAcceso.qml la asignan a su
// ventana (palette: PaletaOscura {}) y todos los controles estandar la heredan (estilo Fusion, fijado
// en main.cpp; el estilo nativo de Windows ignora la paleta). Los colores de arriba valen para los
// grupos activo e inactivo (ventana con o sin foco); el grupo deshabilitado se fija aparte, con un
// gris claro legible sobre el fondo oscuro, para que ningun CheckBox, RadioButton, Button plano ni
// texto de un control estandar quede con el color del sistema (oscuro en el tema claro de Windows).
// El estado con el mouse encima usa los mismos colores de texto.
Palette {
    window: "#0d1117"
    windowText: "#e6edf3"
    base: "#0d1117"
    alternateBase: "#161b22"
    text: "#e6edf3"
    button: "#161b22"
    buttonText: "#c9d1d9"
    brightText: "#ffffff"
    highlight: "#3b82f6"
    highlightedText: "#ffffff"
    placeholderText: "#484f58"
    mid: "#30363d"
    dark: "#0d1117"
    light: "#30363d"
    midlight: "#21262d"
    shadow: "#010409"
    toolTipBase: "#161b22"
    toolTipText: "#e6edf3"
    link: "#58a6ff"
    linkVisited: "#a371f7"

    disabled.windowText: "#8b949e"
    disabled.text: "#8b949e"
    disabled.buttonText: "#8b949e"
    disabled.placeholderText: "#484f58"
    disabled.brightText: "#8b949e"
    disabled.highlightedText: "#8b949e"
}
