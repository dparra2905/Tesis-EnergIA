import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"

// Tema oscuro (revision del PR #8): en todas las paginas, el texto de los controles estandar
// (CheckBox, RadioButton, Button, tambien los planos) es claro sobre el fondo oscuro, habilitado y
// deshabilitado, porque la paleta comun (PaletaOscura.qml) cubre todos los grupos de color.
Item {
    id: raiz
    width: 400
    height: 300

    Component { id: compSesion; SesionFalsa {} }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compSubidas; SubidasFalsa {} }
    Component { id: compRemoto; RemotoFalso {} }
    Component { id: compMain; Main {} }
    Component { id: compAcceso; VentanaAcceso {} }

    // Luminancia relativa (0 = negro, 1 = blanco)
    function luminancia(c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

    // El texto del control sale de la paleta (no es un control con colores propios, como el boton
    // verde de "Nuevo experimento", que lleva texto oscuro a proposito)
    function dePaleta(c) {
        var col = String(c.contentItem.color), p = c.palette;
        return [p.windowText, p.buttonText, p.text, p.brightText].map(String).indexOf(col) >= 0;
    }

    // Controles estandar con texto: tienen palette, text y un contentItem con color
    function controles(item, lista) {
        if (item.palette !== undefined && item.text !== undefined && item.contentItem && item.contentItem.color !== undefined
            && String(item.text) !== "" && item.indicator !== undefined && dePaleta(item))
            lista.push(item);
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++) controles(hijos[i], lista);
        return lista;
    }

    // Contraste WCAG entre dos colores (1 = iguales, 21 = negro sobre blanco)
    function contraste(a, b) {
        function lin(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
        function lum(c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }
        var la = lum(a), lb = lum(b);
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
    }

    // Casillas de verificación (CheckBox y Casilla): tienen checkState e indicador
    function casillas(item, lista) {
        if (item.checkState !== undefined && item.indicator !== undefined && item.indicator !== null)
            lista.push(item);
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++) casillas(hijos[i], lista);
        return lista;
    }

    TestCase {
        name: "Paleta"
        when: windowShown

        function revisar(contenedor, pagina) {
            var lista = raiz.controles(contenedor, []);
            for (var i = 0; i < lista.length; i++) {
                var c = lista[i];
                var antes = c.enabled;
                verify(raiz.luminancia(c.contentItem.color) > 0.45,
                       pagina + ": texto claro en '" + c.text + "' (" + c.contentItem.color + ")");
                verify(raiz.luminancia(c.palette.inactive.windowText) > 0.45 && raiz.luminancia(c.palette.inactive.buttonText) > 0.45,
                       pagina + ": ventana sin foco, '" + c.text + "'");
                c.enabled = false;
                verify(raiz.luminancia(c.contentItem.color) > 0.3,
                       pagina + ": deshabilitado sigue legible, '" + c.text + "' (" + c.contentItem.color + ")");
                c.enabled = antes;
            }
            return lista.length;
        }

        function test_todas_las_paginas() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var h = createTemporaryObject(compHistorial, raiz);
            var rem = createTemporaryObject(compRemoto, raiz);
            rem.cargarComparacion([1, 2]);
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz), historial: h,
                                                          sesion: s, remoto: rem, subidas: createTemporaryObject(compSubidas, raiz) });
            var total = 0;
            var paginas = ["dashboard", "nuevo_experimento", "experimentos", "resultados", "base_datos", "comparar", "configuracion"];
            for (var i = 0; i < paginas.length; i++) {
                w.currentPage = paginas[i];
                wait(50);
                total += revisar(w.contentItem, paginas[i]);
            }
            verify(total >= 5, "se revisaron los controles de las paginas (" + total + ")");
            // Los de Nuevo experimento que se reportaron
            w.currentPage = "nuevo_experimento";
            wait(50);
            for (var k of ["guardarModelo", "inferenciaActivada", "botonAvanzadoInferencia"])
                verify(raiz.luminancia(findChild(w.contentItem, k).contentItem.color) > 0.45, k);
        }

        // Una casilla sin marcar se distingue de la tarjeta oscura (#161b22) y del fondo (#0d1117):
        // su cuadro es de Casilla.qml, con un borde claro, y no el de Fusion, que casi no se veía
        function test_casillas_sin_marcar_se_ven() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                          historial: createTemporaryObject(compHistorial, raiz), sesion: s,
                                                          remoto: createTemporaryObject(compRemoto, raiz),
                                                          subidas: createTemporaryObject(compSubidas, raiz) });
            var total = 0;
            for (var pagina of ["nuevo_experimento", "base_datos"]) {
                w.currentPage = pagina;
                wait(50);
                var lista = raiz.casillas(w.contentItem, []);
                for (var i = 0; i < lista.length; i++) {
                    var c = lista[i], antes = c.checked;
                    c.checked = false;
                    var cuadro = c.indicator;
                    compare(cuadro.objectName, "cuadroCasilla", pagina + ": '" + c.text + "' usa Casilla.qml");
                    for (var fondo of ["#161b22", "#0d1117"])
                        verify(raiz.contraste(cuadro.border.color, Qt.color(fondo)) >= 3,
                               pagina + ": borde de '" + c.text + "' (" + cuadro.border.color + ") sobre " + fondo);
                    c.checked = true;
                    verify(raiz.contraste(cuadro.color, Qt.color("#161b22")) >= 3, pagina + ": marcada se rellena");
                    c.checked = antes;
                    total++;
                }
            }
            verify(total >= 3, "se revisaron las casillas (" + total + ")");
        }

        function test_acceso() {
            var w = createTemporaryObject(compAcceso, raiz, { sesion: createTemporaryObject(compSesion, raiz) });
            wait(50);
            revisar(w.contentItem, "acceso");
            compare(String(w.palette.windowText), "#e6edf3", "la ventana de acceso usa la paleta comun");
        }
    }
}
