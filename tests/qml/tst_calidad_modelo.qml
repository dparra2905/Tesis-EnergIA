import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// Metricas del modelo (D-015): seccion "Desempeño del modelo" de Resultados (local y remoto) y las
// columnas nuevas de Comparar. Tambien la nota de energia en bruto y la potencia de la base previa
// (revision del PR de la inferencia).
Item {
    id: raiz
    width: 1300
    height: 1000

    AppState { id: estadoComun }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compRemoto; RemotoFalso {} }
    Component { id: compSubidas; SubidasFalsa {} }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: compComparar; Comparar { anchors.fill: parent } }
    Component { id: compCalidad; CalidadModelo { width: 900 } }

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
        name: "CalidadModelo"
        when: windowShown

        // Resultados es un Flickable: lleva el control a la vista (despues del layout) y hace clic
        function clicEnVista(pagina, item) {
            wait(20);
            pagina.contentY = Math.max(0, item.mapToItem(pagina.contentItem, 0, 0).y - 40);
            wait(20);
            mouseClick(item);
        }

        function test_local_validacion_y_entrenamiento_con_error() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[0].dir });
            var c = findChild(r, "calidadModelo");
            verify(c.visible && c.disponible, "la seccion aparece con las metricas de la corrida");
            compare(c.conjunto, "validacion", "empieza en validacion");
            compare(findChild(c, "statExactitud").value, "80.0 %");
            compare(findChild(c, "statPromedioMacro").value, "F1 0.444");
            verify(findChild(c, "statPromedioPonderado").caption.indexOf("precisión 0.640") === 0);
            verify(findChild(c, "matrizConfusion").visible);
            compare(findChild(c, "matrizConfusion").columns, 3, "2 clases + la columna de etiquetas");
            verify(raiz.contieneTexto(c, "real \\ predicha"));
            verify(raiz.contieneTexto(c, "80.0 %") && raiz.contieneTexto(c, "240"), "conteo y porcentaje por fila");
            verify(raiz.contieneTexto(c, "0.000 *"), "la precision indefinida lleva la marca");
            verify(findChild(c, "notaIndefinidas").visible);
            verify(!findChild(c, "calidadError").visible);

            // Entrenamiento: predecir fallo en ese conjunto (la seccion se lleva a la vista para el clic)
            clicEnVista(r, findChild(c, "conjunto_entrenamiento"));
            compare(c.conjunto, "entrenamiento");
            verify(findChild(c, "calidadError").visible);
            compare(findChild(c, "calidadError").text,
                    "No se pudieron calcular las métricas: predecir devolvio: error interno de la DLL");
            verify(!findChild(c, "matrizConfusion").visible && !findChild(c, "tablaPorClase").visible,
                   "sin matriz ni tabla en el conjunto que fallo");
            clicEnVista(r, findChild(c, "conjunto_validacion"));
            verify(findChild(c, "matrizConfusion").visible);
        }

        function test_local_sin_metricas_no_disponible() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[1].dir });
            var c = findChild(r, "calidadModelo");
            verify(c.visible && !c.disponible);
            verify(findChild(c, "calidadNoDisponible").visible);
            verify(findChild(c, "calidadNoDisponible").text.indexOf("No disponible") === 0);
            verify(!findChild(c, "selectorConjunto").visible && !findChild(c, "matrizConfusion").visible);
        }

        function test_remoto_desde_la_copia_de_la_base() {
            var rem = createTemporaryObject(compRemoto, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, remoto: rem, modoRemoto: true,
                                                                  subidas: createTemporaryObject(compSubidas, raiz) });
            rem.cargarDetalle(1);
            rem.entregarDetalle(1);
            var c = findChild(r, "calidadModelo");
            verify(c.disponible, "modo remoto: metricas_modelo de la base");
            compare(findChild(c, "statExactitud").value, "83.0 %");
            compare(findChild(c, "matrizConfusion").columns, 4);
            clicEnVista(r, findChild(c, "conjunto_entrenamiento"));
            compare(findChild(c, "statExactitud").value, "95.0 %");
            verify(!findChild(c, "notaIndefinidas").visible);
            // Corrida subida antes de la 009: no disponible
            rem.cargarDetalle(2);
            rem.entregarDetalle(2);
            verify(!c.disponible && findChild(c, "calidadNoDisponible").visible);
        }

        function test_comparar_calidad_y_base_previa() {
            var rem = createTemporaryObject(compRemoto, raiz);
            rem.cargarComparacion([1, 2]);
            var p = createTemporaryObject(compComparar, raiz, { appState: estadoComun, remoto: rem });
            var fila = function (t) { for (var i = 0; i < p.filas.length; i++) if (p.filas[i].t === t) return p.filas[i]; return null; };
            compare(fila("Exactitud en validación").f(p.corridas[0]), "83.00 %");
            compare(fila("F1 macro en validación").f(p.corridas[0]), "0.812");
            compare(fila("Exactitud en entrenamiento").f(p.corridas[0]), "95.00 %");
            compare(fila("Entrenamiento − validación (sobreajuste)").f(p.corridas[0]), "+12.00 puntos");
            compare(fila("F1 macro en validación").f(p.corridas[1]), "—", "corrida sin metricas: —");
            compare(fila("Entrenamiento − validación (sobreajuste)").f(p.corridas[1]), "—");
            compare(fila("Potencia media de la base previa").f(p.corridas[0]), "18.5 W");
            compare(fila("Potencia media de la base previa").f(p.corridas[1]), "21.0 W");
            verify(findChild(p, "notaEnBrutoComparar").visible);
            verify(findChild(p, "notaEnBrutoComparar").text.indexOf("En bruto: incluye el consumo del equipo en reposo") >= 0);
        }

        function test_resultados_energia_en_bruto_y_base_previa() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[0].dir });
            verify(findChild(r, "notaEnBrutoEntrenamiento").visible);
            compare(findChild(r, "notaEnBrutoEntrenamiento").text,
                    "En bruto: incluye el consumo del equipo en reposo (la corrección por línea base no está decidida, D-008).");
            compare(findChild(r, "statPotenciaEntrenamiento").caption, "base previa (reposo): 18.5 W");
            verify(findChild(r, "statEnergia").caption.indexOf("en bruto") >= 0);
            // Inferencia: nota, energia por prediccion en bruto y potencia junto a la base previa
            h.corridas = [h.corridaConInferencia];
            r.dirCorrida = h.corridas[0].dir;
            verify(findChild(r, "notaEnBrutoInferencia").visible);
            compare(findChild(r, "statEnergiaPrediccion").caption, "en bruto (D-008)");
            verify(findChild(r, "statPotenciaInferencia").visible);
            compare(findChild(r, "statPotenciaInferencia").caption, "base previa (reposo): 18.5 W");
        }

        // Una sola sección de desempeño en Resultados: la exactitud en validación no se repite
        function test_resultados_muestra_la_exactitud_una_vez() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[0].dir });
            var c = findChild(r, "calidadModelo");
            compare(c.title, "Desempeño del modelo");
            verify(findChild(r, "grupoModelo") === null, "no queda la sección separada de arriba");
            verify(findChild(r, "statModelo_val_accuracy") === null, "la exactitud no se repite como tarjeta aparte");
            var e = findChild(c, "statExactitud");
            compare(e.title, "Exactitud en validación");
            compare(e.caption, "val_accuracy = 0.8000", "el valor original de evaluar() va como pie");
        }

        // La pérdida en validación (MLP) entra como tarjeta en Validación y no en Entrenamiento
        function test_perdida_en_validacion_junto_a_la_exactitud() {
            var h = createTemporaryObject(compHistorial, raiz);
            var c = createTemporaryObject(compCalidad, raiz, {
                metricas: h.metricasModelo,
                evaluacion: [{ clave: "val_accuracy", titulo: "Exactitud en validación", valor: "80.00 %", detalle: "val_accuracy = 0.8000" },
                             { clave: "val_loss", titulo: "Pérdida en validación", valor: "0.4321", detalle: "val_loss (sin unidad)" }]
            });
            var perdida = findChild(c, "statModelo_val_loss");
            verify(perdida !== null && perdida.visible);
            compare(perdida.value, "0.4321");
            c.conjunto = "entrenamiento";
            wait(0);
            verify(findChild(c, "statModelo_val_loss") === null, "en Entrenamiento no hay pérdida en validación");
            compare(findChild(c, "statExactitud").title, "Exactitud en entrenamiento");
            compare(findChild(c, "statExactitud").caption, "");
        }

        // Corrida anterior a las métricas del modelo: se conserva la evaluación de la DLL
        function test_sin_metricas_muestra_la_evaluacion() {
            var c = createTemporaryObject(compCalidad, raiz, {
                metricas: ({}),
                evaluacion: [{ clave: "val_accuracy", titulo: "Exactitud en validación", valor: "83.00 %", detalle: "val_accuracy = 0.8300" }]
            });
            verify(!c.disponible);
            var acc = findChild(c, "statModelo_val_accuracy");
            verify(acc !== null && acc.visible);
            compare(acc.value, "83.00 %");
            verify(acc.ayuda.length > 0);
            verify(findChild(c, "calidadNoDisponible").text.indexOf("Sin matriz de confusión") === 0);
            verify(!findChild(c, "selectorConjunto").visible);
        }
    }
}
