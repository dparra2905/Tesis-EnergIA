import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// F4: la vista de ejecucion refleja el estado del cliente y envia sus ordenes.
Item {
    id: raiz
    width: 1200
    height: 900

    AppState { id: estadoComun }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compPagina; ExperimentoEnEjecucion { anchors.fill: parent } }
    Component { id: spyComp; SignalSpy {} }

    TestCase {
        name: "ExperimentoEnEjecucion"
        when: windowShown

        function crear(cliente) {
            var pagina = createTemporaryObject(compPagina, raiz, {
                appState: estadoComun,
                cliente: cliente
            });
            verify(pagina !== null);
            return pagina;
        }

        function test_sin_corrida() {
            var pagina = crear(createTemporaryObject(compCliente, raiz));
            compare(pagina.hayCorrida, false);
            pagina = crear(null);
            compare(pagina.hayCorrida, false);
        }

        function test_corrida_en_curso_y_cancelar() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.fase = "entrenando";
            cliente.totalPasos = 5;
            cliente.pasosCompletados = 3;
            cliente.unidadPaso = "paso";
            cliente.corrida = { experimento: "prueba_f4", corrida: 2, modelo_nombre: "Prueba (simulado)",
                                t_start: new Date(Date.now() - 65000).toISOString() };
            var pagina = crear(cliente);
            verify(pagina.hayCorrida);
            compare(findChild(pagina, "textoProgreso").text, "3 / 5 · paso");
            compare(pagina.duracion().substring(0, 7), "00:01:0");

            var cancelar = findChild(pagina, "botonCancelar");
            verify(cancelar.visible);
            compare(findChild(pagina, "botonForzar").visible, false);
            mouseClick(cancelar);
            compare(cliente.llamadasCancelar, 1);
        }

        function test_linea_base_en_el_resumen() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.corrida = { experimento: "prueba", modelo_nombre: "Prueba (simulado)", linea_base_s: 20 };
            var pagina = crear(cliente);
            compare(findChild(pagina, "resumenLineaBase").description, "20 s antes y 20 s después del entrenamiento");
            cliente.corrida = { experimento: "prueba", modelo_nombre: "Prueba (simulado)", linea_base_s: 0 };
            compare(findChild(pagina, "resumenLineaBase").description, "Sin línea base");
            compare(findChild(pagina, "textoInferencia").visible, false, "sin inferencia no se muestra su progreso");
        }

        function test_inferencia_en_curso() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.fase = "fase_media";
            cliente.totalPasos = 5;
            cliente.pasosCompletados = 5;
            cliente.unidadPaso = "paso";
            cliente.corrida = { experimento: "prueba", modelo_nombre: "Prueba (simulado)", linea_base_s: 20,
                                inferencia: { activada: true, lote: 65536, duracion_min_s: 60 } };
            var pagina = crear(cliente);
            compare(findChild(pagina, "resumenLineaBase").description,
                    "20 s antes, 20 s entre el entrenamiento y la inferencia, y 20 s después");
            verify(findChild(pagina, "textoInferencia").visible);
            compare(findChild(pagina, "textoInferencia").text, "Inferencia: línea base intermedia en curso");
            cliente.fase = "inferencia";
            cliente.progresoInferencia = { grupos: 3, pasadas: 1200, predicciones: 360000 };
            compare(findChild(pagina, "textoInferencia").text, "Inferencia: grupo 3 · pasada 1200 · 360000 predicciones",
                    "numero de pasada y predicciones acumuladas");
            verify(findChild(pagina, "botonCancelar").visible, "la inferencia se puede cancelar como el entrenamiento");
        }

        function test_metricas_del_monitor_de_software() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.medidoresCorrida = [{ nombre: "sistema", estado: "lanzado" }];
            var pagina = crear(cliente);
            compare(findChild(pagina, "estadoMonitorSoftware").status, "Midiendo");
            compare(pagina.hayMetricas, false, "antes de la primera fila no hay valores");

            cliente.metricasSistema = { timestamp: "2026-10-01T10:00:05.000Z", cpu_proceso_pct: 12.5, cpu_sistema_pct: 30.25,
                                        ram_proceso_mb: 100.4, ram_sistema_pct: 50 };
            cliente.historialSistema = { cpu_proceso_pct: [12, 12.5], cpu_sistema_pct: [29, 30.25],
                                         ram_proceso_mb: [99, 100.4], ram_sistema_pct: [50, 50] };
            verify(pagina.hayMetricas);
            compare(findChild(pagina, "metrica_cpu_proceso_pct").value, "12.5");
            compare(findChild(pagina, "metrica_cpu_sistema_pct").value, "30.3");
            compare(findChild(pagina, "metrica_ram_proceso_mb").value, "100");
            compare(findChild(pagina, "metrica_ram_sistema_pct").value, "50.0");
            compare(findChild(pagina, "metrica_cpu_proceso_pct").values.length, 2, "serie para la grafica");
            var graficaProceso = findChild(pagina, "graficaRamProceso");
            var graficaSistema = findChild(pagina, "graficaRamSistema");
            compare(findChild(pagina, "tarjetaGraficaRam").visible, true);
            compare(graficaProceso.unidad, "MB");
            compare(graficaSistema.unidad, "%");
            compare(graficaProceso.series[0].values[1], 100.4);
            compare(graficaSistema.series[0].values[1], 50);
            cliente.historialSistema = { ram_proceso_mb: [99, 100.4, 120.8],
                                         ram_sistema_pct: [50, 50, 52] };
            compare(graficaProceso.series[0].values[2], 120.8, "la grafica sigue las lecturas entrantes");
            compare(graficaSistema.series[0].values[2], 52, "uso general del sistema separado del proceso");

            cliente.medidoresCorrida = [{ nombre: "sistema", estado: "termino_antes" }];
            compare(findChild(pagina, "estadoMonitorSoftware").status, "Terminó antes de tiempo");
        }

        function test_metricas_de_energia() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.medidoresCorrida = [{ nombre: "energia", estado: "lanzado" }];
            cliente.metricasEnergia = { conectado: true, potencia_w: 41.25, voltaje_v: 220.4, corriente_a: 0.1874 };
            cliente.historialEnergia = { potencia_w: [40, 41.25], voltaje_v: [220.1, 220.4], corriente_a: [0.18, 0.1874] };
            var pagina = crear(cliente);
            compare(findChild(pagina, "estadoMonitorEnergia").status, "Midiendo");
            compare(findChild(pagina, "metrica_potencia_w").value, "41.3");
            compare(findChild(pagina, "metrica_voltaje_v").value, "220.4");
            compare(findChild(pagina, "metrica_corriente_a").value, "0.187");
            compare(findChild(pagina, "metrica_energia").value, "Al terminar",
                    "durante la corrida no se muestra energía: se calcula al terminar (D-008)");
            compare(findChild(pagina, "avisoConexionEnergia").visible, false);

            cliente.metricasEnergia = { conectado: false };
            compare(findChild(pagina, "avisoConexionEnergia").visible, true, "aviso de lectura sin conexión");
            compare(findChild(pagina, "metrica_potencia_w").value, "—", "sin trama vigente no se muestra potencia");

            cliente.estado = "completada";
            cliente.corrida = { energia_experimento: { entrenamiento: { energia_wh: 0.0333333, cobertura: 1, duracion_s: 3, huecos: [] } } };
            compare(findChild(pagina, "metrica_energia").value, "0.0333", "al terminar: energía del experimento de resultados.json");
        }

        function test_corrida_sin_monitor_de_software() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            var pagina = crear(cliente);
            compare(pagina.estadoMedidor("sistema"), "");
            compare(findChild(pagina, "estadoMonitorSoftware").status, "No incluido");
            compare(pagina.hayMetricas, false);
            compare(findChild(pagina, "tarjetaGraficaRam").visible, false,
                    "sin monitor de software no se dibuja la serie de RAM");
        }

        function test_resumen_separa_entrada_experimento_y_salida() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "en_curso";
            cliente.activa = true;
            cliente.medidoresCorrida = [{ nombre: "sistema", estado: "lanzado" }];
            cliente.corrida = { modelo_nombre: "Modelo A", datos_descripcion: "archivo.csv",
                                linea_base_s: 20, dir_corrida: "C:/resultados/corrida_001" };
            var pagina = crear(cliente);
            verify(findChild(pagina, "grupoEntradaEjecucion") !== null);
            verify(findChild(pagina, "grupoExperimentoEjecucion") !== null);
            verify(findChild(pagina, "grupoSalidaEjecucion") !== null);
            verify(findChild(pagina, "resumenMonitoresSalida").description.indexOf("Software: Midiendo") >= 0);
            verify(findChild(pagina, "resumenResultadoSalida").description.indexOf("Pendiente:") === 0,
                   "antes de terminar no se presenta una evaluaci?n inexistente");
            cliente.estado = "completada";
            cliente.activa = false;
            cliente.corrida = { evaluacion: { ejecutada: true, metricas: { accuracy: 0.87 } } };
            verify(findChild(pagina, "resumenResultadoSalida").description.indexOf("accuracy = 0.8700") >= 0);
        }

        function test_cancelando_ofrece_forzar() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "cancelando";
            cliente.activa = true;
            var pagina = crear(cliente);
            compare(findChild(pagina, "botonCancelar").visible, false);
            verify(findChild(pagina, "botonForzar").visible);
        }

        function test_error_muestra_el_mensaje() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "error";
            cliente.corrida = { error: { tipo: "proceso", mensaje: "El proceso de ejecucion termino de forma anormal." } };
            var pagina = crear(cliente);
            compare(findChild(pagina, "mensajeError").text, "El proceso de ejecucion termino de forma anormal.");
            verify(findChild(pagina, "botonNuevo").visible);
        }

        function test_terminada_permite_un_nuevo_experimento() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.estado = "completada";
            cliente.totalPasos = 5;
            cliente.pasosCompletados = 5;
            cliente.unidadPaso = "paso";
            cliente.corrida = { experimento: "prueba_f4", t_start: "2026-10-01T10:00:00.000Z", t_fin: "2026-10-01T10:00:07.250Z" };
            var pagina = crear(cliente);
            compare(pagina.duracion(), "00:00:07", "la duracion se congela en t_fin");
            var spy = createTemporaryObject(spyComp, raiz, { target: pagina, signalName: "nuevoExperimentoSolicitado" });
            var nuevo = findChild(pagina, "botonNuevo");
            verify(nuevo.visible);
            mouseClick(nuevo);
            compare(cliente.llamadasDescartar, 1);
            compare(spy.count, 1);
        }
    }
}
