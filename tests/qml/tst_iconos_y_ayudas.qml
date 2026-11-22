import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"
import "../../src/ui/qml/components/Iconos.js" as Iconos
import "../../src/ui/qml/components/Ayudas.js" as Ayudas

// Iconos vectoriales (sin emojis) y ayudas breves de las métricas; Resultados separa el
// desempeño del modelo, la energía y los recursos (observaciones de la revisión de la interfaz).
Item {
    id: raiz
    width: 1300
    height: 1000

    AppState { id: estadoComun }
    Component { id: compIcono; Icono {} }
    Component { id: compBoton; BotonIcono {} }
    Component { id: compAyuda; Ayuda {} }
    Component { id: compEnlace; Enlace {} }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: spyComp; SignalSpy {} }

    function contieneTexto(item, patron) {
        if (item.text !== undefined && String(item.text).indexOf(patron) >= 0 && item.visible) return true;
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++)
            if (contieneTexto(hijos[i], patron)) return true;
        return false;
    }

    TestCase {
        name: "IconosYAyudas"
        when: windowShown

        function test_todos_los_iconos_se_crean() {
            var nombres = Object.keys(Iconos.trazos);
            verify(nombres.length >= 30);
            for (var i = 0; i < nombres.length; i++) {
                var ic = createTemporaryObject(compIcono, raiz, { nombre: nombres[i], tamano: 16 });
                verify(ic !== null, nombres[i]);
                verify(ic.trazo.length > 0, nombres[i] + " tiene trazo");
                compare(ic.implicitWidth, 16);
            }
        }

        function test_icono_desconocido_no_dibuja_nada() {
            var ic = createTemporaryObject(compIcono, raiz, { nombre: "no_existe" });
            compare(ic.trazo, "");
        }

        function test_boton_con_icono_conserva_texto_y_clic() {
            var b = createTemporaryObject(compBoton, raiz, { icono: "descargar", text: "Exportar" });
            compare(b.text, "Exportar", "el texto sigue siendo el nombre accesible");
            verify(raiz.contieneTexto(b.contentItem, "Exportar"));
            var spy = createTemporaryObject(spyComp, raiz, { target: b, signalName: "clicked" });
            mouseClick(b);
            compare(spy.count, 1);
        }

        function test_enlace_con_icono() {
            var e = createTemporaryObject(compEnlace, raiz, { icono: "atras", text: "Base de datos" });
            var spy = createTemporaryObject(spyComp, raiz, { target: e, signalName: "clicked" });
            mouseClick(e);
            compare(spy.count, 1);
            e.enabled = false;
            mouseClick(e);
            compare(spy.count, 1, "deshabilitado no responde");
        }

        function test_ayuda_muestra_su_texto_al_pasar_el_raton() {
            var a = createTemporaryObject(compAyuda, raiz, { texto: Ayudas.texto("potencia_w"), x: 100, y: 100 });
            var tip = findChild(a, "textoAyuda");
            compare(tip.visible, false);
            mouseMove(a, a.width / 2, a.height / 2);
            tryCompare(tip, "visible", true);
            verify(tip.text.indexOf("W") >= 0);
        }

        function test_ayudas_no_confunden_recursos_con_energia() {
            verify(Ayudas.texto("grupo_recursos").indexOf("No son energía") >= 0);
            verify(Ayudas.texto("energia").indexOf("Wh") >= 0);
            verify(Ayudas.texto("duracion").indexOf("D-003") >= 0);
            compare(Ayudas.texto("no_existe"), "");
        }

        function test_resultados_separa_modelo_energia_y_recursos() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            var modelo = findChild(r, "calidadModelo");
            verify(modelo.visible);
            verify(modelo.ayuda.length > 0);
            verify(findChild(r, "grupoEnergia").text.indexOf("Monitor físico de energía") >= 0);
            verify(findChild(r, "grupoRecursos").text.indexOf("Monitor de software") >= 0);
            var acc = findChild(modelo, "statExactitud");
            compare(acc.value, "80.0 %");
            compare(acc.caption, "val_accuracy = 0.8000");
            verify(acc.ayuda.length > 0);
            // El modelo va antes que la energía y la energía antes que los recursos
            var yModelo = modelo.mapToItem(r.contentItem, 0, 0).y;
            var yEnergia = findChild(r, "grupoEnergia").mapToItem(r.contentItem, 0, 0).y;
            var yRecursos = findChild(r, "grupoRecursos").mapToItem(r.contentItem, 0, 0).y;
            verify(yModelo < yEnergia && yEnergia < yRecursos, "orden: modelo, energía, recursos");
        }

        function test_resultados_sin_evaluacion() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[1].dir });
            compare(r.metricasModelo.length, 0);
            verify(raiz.contieneTexto(r, "no tiene métricas del modelo"));
        }
    }
}
