import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// Las cinco paginas se crean sin errores, con y sin cliente del orquestador.
Item {
    id: raiz
    width: 1200
    height: 900

    AppState { id: estadoComun }
    Component { id: compEstado; AppState {} }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compDashboard; Dashboard { anchors.fill: parent } }
    Component { id: compNuevo; NuevoExperimento { anchors.fill: parent } }
    Component { id: compEjecucion; ExperimentoEnEjecucion { anchors.fill: parent } }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: compConfiguracion; Configuracion { anchors.fill: parent } }

    TestCase {
        name: "Paginas"
        when: windowShown

        function test_paginas_data() {
            return [
                { tag: "Dashboard", comp: compDashboard, usaCliente: false },
                { tag: "NuevoExperimento", comp: compNuevo, usaCliente: true },
                { tag: "ExperimentoEnEjecucion", comp: compEjecucion, usaCliente: true },
                { tag: "Resultados", comp: compResultados, usaCliente: false },
                { tag: "Configuracion", comp: compConfiguracion, usaCliente: true }
            ];
        }

        function test_paginas(d) {
            var estado = estadoComun;
            var p = createTemporaryObject(d.comp, raiz, { appState: estado });
            verify(p !== null, d.tag + " sin cliente");
            if (d.usaCliente) {
                var q = createTemporaryObject(d.comp, raiz, { appState: estado, cliente: createTemporaryObject(compCliente, raiz) });
                verify(q !== null, d.tag + " con cliente");
            }
        }

        function test_configuracion_muestra_el_laboratorio() {
            var c = createTemporaryObject(compCliente, raiz);
            var p = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: c });
            verify(findChild(p, "laboratorioEnergia").visible);
            verify(findChild(p, "estadoEntornoEnergia").text.indexOf("Entorno de Python del medidor") === 0);
            compare(findChild(p, "iconoEntornoEnergia").nombre, "check");
            var sin = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun });
            compare(findChild(sin, "laboratorioEnergia").visible, false, "sin backend no se muestra");
        }

        function test_configuracion_etiqueta_carpetas_entrada_y_salida() {
            var c = createTemporaryObject(compCliente, raiz);
            var p = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: c });
            var datasets = findChild(p, "carpetaDatasets");
            var modelos = findChild(p, "carpetaModelos");
            var resultados = findChild(p, "carpetaResultados");
            compare(datasets.categoria, "DATOS DE ENTRADA");
            compare(modelos.categoria, "RECURSOS DE ENTRADA");
            compare(resultados.categoria, "DATOS DE SALIDA");
            verify(datasets.detalle.indexOf("entrenar") >= 0);
            verify(resultados.detalle.indexOf("se guardan") >= 0);
            compare(datasets.tipo, "datasets");
            compare(resultados.tipo, "resultados");
        }

        function test_configuracion_carpetas_separadas_y_sin_cambios_en_corrida() {
            var c = createTemporaryObject(compCliente, raiz);
            var p = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: c });
            var salidas = findChild(p, "carpetaResultados");
            var modelos = findChild(p, "carpetaModelos");
            var datasets = findChild(p, "carpetaDatasets");
            verify(salidas !== null && modelos !== null && datasets !== null);
            compare(salidas.ruta, c.dirSalida);
            compare(modelos.ruta, c.dirModelos);
            compare(datasets.ruta, c.dirDatasets);
            verify(findChild(p, "tarjetaMonitores") !== null);
            verify(findChild(p, "tarjetaEjecucion") !== null);
            verify(findChild(p, "tarjetaEquipo") !== null);
            var boton = findChild(p, "examinar_resultados");
            verify(boton.enabled);
            c.activa = true;
            compare(boton.enabled, false, "no se cambia carpeta durante corrida");
            c.activa = false;
            compare(boton.enabled, true);
        }

        function test_configuracion_linea_base_por_defecto() {
            var c = createTemporaryObject(compCliente, raiz);
            var p = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: c });
            var campo = findChild(p, "lineaBaseDefecto");
            compare(campo.value, 20, "20 s por defecto");
            compare(campo.to, 3600);
            campo.forceActiveFocus();
            keyClick(Qt.Key_Up);
            compare(c.lineaBaseSegundos, 21, "el cambio llega al cliente, que lo guarda");
            c.lineaBaseSegundos = 45;
            compare(campo.value, 45, "el campo muestra el valor del cliente");
        }

        function test_notificacion_real() {
            var estado = createTemporaryObject(compEstado, raiz);
            var antes = estado.notifications.count;
            var noLeidas = estado.unreadCount;
            estado.agregarNotificacion("success", "Experimento completado", "La corrida termino.");
            compare(estado.notifications.count, antes + 1);
            compare(estado.notifications.get(0).title, "Experimento completado");
            compare(estado.notifications.get(0).read, false);
            compare(estado.unreadCount, noLeidas + 1);
        }
    }
}
