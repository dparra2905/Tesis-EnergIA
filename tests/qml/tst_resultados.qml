import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// F9: Dashboard y Resultados muestran las corridas del historial, sin datos inventados.
Item {
    id: raiz
    width: 1300
    height: 1000

    AppState { id: estadoComun }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compDashboard; Dashboard { anchors.fill: parent } }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: spyComp; SignalSpy {} }

    // Busca recursivamente un texto visible que contenga 'patron'
    function contieneTexto(item, patron) {
        if (item.text !== undefined && String(item.text).indexOf(patron) >= 0 && item.visible) return true;
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++)
            if (contieneTexto(hijos[i], patron)) return true;
        if (item.contentItem && item.contentItem !== item && contieneTexto(item.contentItem, patron)) return true;
        return false;
    }

    TestCase {
        name: "Resultados"
        when: windowShown

        function test_dashboard_lista_las_corridas() {
            var h = createTemporaryObject(compHistorial, raiz);
            var d = createTemporaryObject(compDashboard, raiz, { appState: estadoComun, historial: h });
            compare(findChild(d, "statTotal").value, "2");
            verify(findChild(d, "statUltima").caption.indexOf("Prueba (simulado)") === 0);
            compare(findChild(d, "tablaCorridas").count, 2);
            verify(raiz.contieneTexto(d, "0.0444 Wh"), "energía del experimento de la corrida");
            verify(!raiz.contieneTexto(d, "SVM") && !raiz.contieneTexto(d, "DecisionTree_Model"), "sin datos mock");

            var spy = createTemporaryObject(spyComp, raiz, { target: d, signalName: "corridaSeleccionada" });
            d.corridaSeleccionada(h.corridas[1].dir);
            compare(spy.count, 1);
        }

        function test_dashboard_sin_backend() {
            var d = createTemporaryObject(compDashboard, raiz, { appState: estadoComun });
            compare(findChild(d, "statTotal").value, "—");
            verify(raiz.contieneTexto(d, "Vista sin backend"));
        }

        function test_resultados_de_la_corrida_mas_reciente() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            verify(r.hayCorrida);
            compare(r.dirEfectivo, h.corridas[0].dir, "sin selección se muestra la más reciente");
            compare(findChild(r, "statCpuProceso").value, "12.5 %");
            compare(findChild(r, "statEnergia").value, "0.0444 Wh");
            var ramProceso = findChild(r, "graficaRamProcesoResultados");
            var ramSistema = findChild(r, "graficaRamSistemaResultados");
            verify(findChild(r, "tarjetaRamResultados").visible);
            compare(ramProceso.unidad, "MB");
            compare(ramSistema.unidad, "%");
            compare(ramProceso.series[0].values[1], 150);
            compare(ramProceso.series[0].values[2], null, "se conservan huecos, sin interpolación");
            compare(ramSistema.series[0].values[2], 51);
            verify(findChild(r, "statEnergia").caption.indexOf("dato derivado") >= 0);
            verify(raiz.contieneTexto(r, "val_accuracy = 0.8000"), "evaluación real de la corrida");
            verify(!raiz.contieneTexto(r, "energéticamente eficiente") && !raiz.contieneTexto(r, "DecisionTree_Model"),
                   "sin el análisis redactado ni el modelo mock");
        }

        function test_resultados_sin_medidores() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[1].dir });
            compare(r.midioSistema, false);
            compare(r.midioEnergia, false);
            compare(findChild(r, "tarjetaRamResultados").visible, false,
                    "sin monitor de software no aparece la grafica de RAM");
            verify(raiz.contieneTexto(r, "no incluyó el monitor de software"));
            verify(raiz.contieneTexto(r, "no incluyó el monitor físico de energía"));
            compare(findChild(r, "botonExportar").enabled, false, "sin medidores no hay rejilla que exportar");
        }

        function test_inferencia_en_resultados() {
            var h = createTemporaryObject(compHistorial, raiz);
            h.corridas = [h.corridaConMedidores, h.corridaConInferencia, h.corridaInferenciaCancelada];
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[0].dir });
            compare(findChild(r, "tarjetaInferencia").visible, false, "sin inferencia no aparece su tarjeta");
            r.dirCorrida = h.corridas[1].dir;
            verify(findChild(r, "tarjetaInferencia").visible, "tarjeta de la inferencia, separada del entrenamiento");
            compare(findChild(r, "statEnergiaInferencia").value, "0.7500 Wh");
            compare(findChild(r, "statEnergiaPrediccion").value, "1.125e-4 J");
            compare(findChild(r, "statTiempoPrediccion").value, "2.517 µs");
            compare(findChild(r, "statPredicciones").value, "24000000");
            verify(findChild(r, "ventanasEnergia").text.indexOf("base intermedia 0.0111 Wh") >= 0
                   && findChild(r, "ventanasEnergia").text.indexOf("inferencia 0.7500 Wh") >= 0,
                   "ventanas base_media e inferencia: " + findChild(r, "ventanasEnergia").text);
            compare(findChild(r, "statEnergia").value, "0.0444 Wh", "la energia del entrenamiento sigue aparte");
            r.dirCorrida = h.corridas[2].dir;
            compare(findChild(r, "statEnergiaPrediccion").value, "— (inferencia cancelada)", "por prediccion null: el motivo");
            compare(findChild(r, "statTiempoPrediccion").value, "— (inferencia cancelada)");
        }

        function test_selector_de_corrida() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            var selector = findChild(r, "selectorCorrida");
            compare(selector.count, 2);
            selector.activated(1);
            compare(r.dirEfectivo, h.corridas[1].dir);
            compare(findChild(r, "estadoResultado").value, "cancelada");
        }

        function test_resultados_sin_corridas() {
            var h = createTemporaryObject(compHistorial, raiz);
            h.corridas = [];
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            compare(r.hayCorrida, false);
            verify(raiz.contieneTexto(r, "Todavía no hay resultados"));
        }
    }
}
