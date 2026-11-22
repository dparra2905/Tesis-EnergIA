import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// F4: el formulario de "Nuevo experimento" se genera desde el catalogo (D-009) y produce la
// seleccion que se envia al cliente del orquestador.
Item {
    id: raiz
    width: 1200
    height: 900

    AppState { id: estadoComun }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compPagina; NuevoExperimento { anchors.fill: parent } }

    TestCase {
        name: "NuevoExperimento"
        when: windowShown

        function crear(cliente) {
            var pagina = createTemporaryObject(compPagina, raiz, {
                appState: estadoComun,
                cliente: cliente
            });
            verify(pagina !== null, "la pagina se crea");
            return pagina;
        }

        function test_sin_cliente_no_permite_iniciar() {
            var pagina = crear(null);
            compare(findChild(pagina, "botonIniciar").enabled, false);
            verify(findChild(pagina, "cajaListo").pendientes.length > 0);
        }

        function test_resumen_separa_entrada_experimento_y_salida() {
            var pagina = crear(createTemporaryObject(compCliente, raiz));
            verify(findChild(pagina, "grupoEntradaNuevo") !== null);
            verify(findChild(pagina, "grupoExperimentoNuevo") !== null);
            verify(findChild(pagina, "grupoSalidaNuevo") !== null);
            verify(findChild(pagina, "resultadoPrevisto").description.indexOf("Al finalizar") === 0,
                   "no se muestran resultados inventados antes de ejecutar");
        }

        function test_formulario_generado_desde_la_dll() {
            var pagina = crear(createTemporaryObject(compCliente, raiz));
            compare(pagina.indiceModelo, 0, "se elige el primer modelo del catalogo");
            compare(findChild(pagina, "campo_pasos").text, "10");
            compare(findChild(pagina, "campo_ms_por_paso").text, "500");
            compare(findChild(pagina, "campo_loss_final").text, "0.2");
            compare(findChild(pagina, "opciones_curva").currentIndex, 0);
            compare(findChild(pagina, "campo_capas").text, "64,32");
            verify(pagina.hiperValidos);
        }

        function test_cambiar_de_modelo_regenera_el_formulario() {
            var pagina = crear(createTemporaryObject(compCliente, raiz));
            pagina.indiceModelo = 1;
            wait(0);
            verify(findChild(pagina, "campo_pasos") === null, "desaparecen los campos del modelo anterior");
            compare(findChild(pagina, "campo_profundidad").text, "5");
        }

        function test_valor_fuera_de_rango_bloquea_el_inicio() {
            var pagina = crear(createTemporaryObject(compCliente, raiz));
            findChild(pagina, "campo_pasos").text = "0";
            compare(pagina.hiperValidos, false);
            compare(findChild(pagina, "botonIniciar").enabled, false);
            findChild(pagina, "campo_pasos").text = "1000";
            compare(pagina.hiperValidos, true);
            findChild(pagina, "campo_capas").text = "64,0";
            compare(pagina.hiperValidos, false, "lista con un elemento fuera de rango");
            findChild(pagina, "campo_capas").text = "1,2,3,4,5,6,7,8,9";
            compare(pagina.hiperValidos, false, "lista con mas elementos de los permitidos");
            findChild(pagina, "campo_capas").text = "128";
            findChild(pagina, "campo_loss_final").text = "5.5";
            compare(pagina.hiperValidos, false, "real fuera de rango");
        }

        // Un real cuyo valor por defecto String() escribe en notación científica (1e-7, como la
        // ganancia mínima del Árbol) nace válido y no bloquea "Iniciar experimento"
        function test_real_en_notacion_cientifica() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.modelos = [{
                archivo: "EnergIA_Arbol.dll", ruta: "C:/energia/bin/EnergIA_Arbol.dll",
                clave: "arbol", nombre: "Árbol de decisión", libreria: "mlpack", unidad_paso: "arbol", hilos: 1, interno: false,
                hiperparametros: [{ clave: "ganancia_min", etiqueta: "Ganancia minima para dividir", ayuda: "",
                                    tipo: "real", minimo: 0.0, maximo: 1.0, por_defecto: 1e-7 }]
            }];
            var pagina = crear(cliente);
            findChild(pagina, "nombreExperimento").text = "arbol";
            var campo = findChild(pagina, "campo_ganancia_min");
            compare(campo.text, "1e-7", "el valor por defecto llega en notación científica");
            verify(pagina.hiperValidos, "1e-7 es válido");
            verify(findChild(pagina, "botonIniciar").enabled, "Iniciar experimento queda habilitado");

            var validos = ["2.5e-3", "1E-5", "1e0", "1e+0", "0.5", "0"];
            for (var i = 0; i < validos.length; i++) {
                campo.text = validos[i];
                verify(pagina.hiperValidos, validos[i] + " es válido");
            }
            var invalidos = ["1e", "e5", "1e-7x", "1.e-3", "2e3", "-1e-3", ""];
            for (var j = 0; j < invalidos.length; j++) {
                campo.text = invalidos[j];
                compare(pagina.hiperValidos, false, "'" + invalidos[j] + "' no es válido");
            }
            campo.text = "1e-7";
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.hiperparametros.ganancia_min, 1e-7, "se envía como número");
        }

        function test_seleccion_enviada_al_cliente() {
            var cliente = createTemporaryObject(compCliente, raiz);
            var pagina = crear(cliente);
            var spy = createTemporaryObject(spyComp, raiz, { target: pagina, signalName: "experimentoIniciado" });
            compare(pagina.origenDatos, "sintetico", "en modo de desarrollo se proponen datos sinteticos");
            findChild(pagina, "campo_pasos").text = "7";
            findChild(pagina, "opciones_curva").currentIndex = 1;
            findChild(pagina, "nombreExperimento").text = "  prueba_f4 ";
            findChild(pagina, "guardarModelo").checked = true;

            var boton = findChild(pagina, "botonIniciar");
            verify(boton.enabled);
            mouseClick(boton);

            var s = cliente.ultimaSeleccion;
            verify(s !== null, "se llamo a iniciar()");
            compare(s.experimento, "prueba_f4");
            compare(s.modelo, cliente.modeloPrueba.ruta);
            compare(s.modelo_nombre, "Prueba (simulado)");
            compare(s.hiperparametros.pasos, 7);
            compare(typeof s.hiperparametros.pasos, "number");
            compare(s.hiperparametros.ms_por_paso, 500);
            compare(s.hiperparametros.loss_final, 0.2);
            compare(s.hiperparametros.curva, "lineal", "las opciones se envian por nombre");
            compare(s.hiperparametros.capas, "64,32");
            compare(s.datos.origen, "sintetico");
            compare(s.porcentaje_entrenamiento, 80);
            compare(s.semilla, 42);
            compare(s.guardar_modelo, true);
            compare(s.linea_base_s, 20, "linea base precargada con el valor de Configuracion");
            compare(s.inferencia.activada, false, "la inferencia esta desactivada por defecto (D-014)");
            compare(spy.count, 1, "la pagina avisa que la corrida se inicio");
        }

        function test_inferencia_en_la_seleccion() {
            var cliente = createTemporaryObject(compCliente, raiz);
            var pagina = crear(cliente);
            findChild(pagina, "nombreExperimento").text = "con_inferencia";
            verify(findChild(pagina, "seccionInferencia").visible, "seccion Inferencia en Nuevo experimento");
            compare(findChild(pagina, "resumenInferencia").description, "Sin inferencia");
            var sinInferencia = pagina.tiempoSinEntrenamientoS();
            compare(sinInferencia, 2 + 2 * 20, "sin inferencia: margen de 2 s y dos lineas base de 20 s");
            compare(findChild(pagina, "avanzadoInferencia").visible, false, "el lote es un campo avanzado, oculto por defecto");

            findChild(pagina, "inferenciaActivada").checked = true;
            compare(findChild(pagina, "inferenciaDuracion").value, 60, "duracion minima por defecto: 60 s");
            compare(findChild(pagina, "inferenciaLote").value, 65536, "lote por defecto: 65.536 filas");
            compare(pagina.tiempoSinEntrenamientoS(), sinInferencia + 20 + 60,
                    "con inferencia: se suman la linea base intermedia y la duracion minima");
            verify(findChild(pagina, "resumenTiempoTotal").description.indexOf("2 min 2 s") >= 0,
                   "tiempo total estimado: " + findChild(pagina, "resumenTiempoTotal").description);
            findChild(pagina, "botonAvanzadoInferencia").clicked();  // fuera de la vista visible: sin mouseClick
            verify(findChild(pagina, "avanzadoInferencia").visible, "Avanzado muestra el lote");
            findChild(pagina, "inferenciaLote").value = 1;
            findChild(pagina, "inferenciaDuracion").value = 30;

            mouseClick(findChild(pagina, "botonIniciar"));
            var s = cliente.ultimaSeleccion;
            compare(s.inferencia.activada, true);
            compare(s.inferencia.duracion_min_s, 30);
            compare(s.inferencia.lote, 1, "con 1 se mide predecir consulta por consulta");
        }

        function test_linea_base_por_corrida() {
            var cliente = createTemporaryObject(compCliente, raiz, { lineaBaseSegundos: 35 });
            var pagina = crear(cliente);
            var campo = findChild(pagina, "lineaBase");
            compare(campo.value, 35, "precargada con el valor por defecto de Configuracion");
            compare(campo.from, 0);
            compare(campo.to, 3600);
            compare(findChild(pagina, "resumenLineaBase").description, "35 s antes y 35 s después del entrenamiento");
            campo.value = 0;
            compare(findChild(pagina, "resumenLineaBase").description, "Sin línea base");
            campo.value = 90;
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.linea_base_s, 90, "la corrida envia su propio valor");
            compare(cliente.lineaBaseSegundos, 35, "cambiarla en la corrida no cambia el valor por defecto");
        }

        function test_monitor_de_software_en_la_seleccion() {
            var cliente = createTemporaryObject(compCliente, raiz);
            var pagina = crear(cliente);
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.medidores.sistema, true, "por defecto se incluye el monitor de software");

            findChild(pagina, "selectorRecursos").toggled(false);
            compare(pagina.incluirSistema, false);
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.medidores.sistema, false, "el usuario puede excluirlo");
            compare(cliente.ultimaSeleccion.medidores.energia, false, "el monitor físico no se pide por defecto");
        }

        function test_monitor_de_energia() {
            var cliente = createTemporaryObject(compCliente, raiz);
            var pagina = crear(cliente);
            compare(pagina.incluirEnergia, false, "desactivado por defecto: sin el Atorch la corrida se cancelaría en t_start");
            findChild(pagina, "selectorEnergia").toggled(true);
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.medidores.energia, true);
            compare(pagina.joinParts(["a", "", "b"], " + ", "-"), "a + b", "resumen de monitores");

            cliente.laboratorio = { mac: "00:00:00:19:EC:2C", python: "C:\\no\\existe", python_existe: false,
                                    script: "C:\\x\\energia_ble.py", script_existe: true };
            var pagina2 = crear(cliente);
            findChild(pagina2, "selectorEnergia").toggled(true);
            compare(findChild(pagina2, "botonIniciar").enabled, false, "sin el entorno de Python del medidor no se puede iniciar");
            verify(findChild(pagina2, "cajaListo").pendientes.join(" ").indexOf("crear_entorno") >= 0);
        }

        function test_error_del_cliente_se_muestra() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.respuestaIniciar = "No se encontro energia_orquestador";
            var pagina = crear(cliente);
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(pagina.mensajeError, "No se encontro energia_orquestador");
        }

        function test_sin_modo_desarrollo_se_exige_un_csv() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.modoDesarrollo = false;
            cliente.modelos = [cliente.modeloFicticio];
            var pagina = crear(cliente);
            compare(pagina.origenDatos, "csv");
            compare(findChild(pagina, "botonIniciar").enabled, false);

            pagina.seleccionarCsv("C:/datos/iris.csv");
            compare(pagina.indiceColumna, 2, "se propone la ultima columna como objetivo");
            verify(findChild(pagina, "botonIniciar").enabled);
            mouseClick(findChild(pagina, "botonIniciar"));
            compare(cliente.ultimaSeleccion.datos.origen, "csv");
            compare(cliente.ultimaSeleccion.datos.ruta, "C:/datos/iris.csv");
            compare(cliente.ultimaSeleccion.datos.columna_objetivo, "etiqueta");
        }

        function test_csv_ilegible_bloquea_el_inicio() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.modoDesarrollo = false;
            cliente.encabezado = { ok: false, columnas: [], separador: ",", error: "no existe" };
            var pagina = crear(cliente);
            pagina.seleccionarCsv("C:/no/existe.csv");
            compare(findChild(pagina, "botonIniciar").enabled, false);
        }

        function test_corrida_en_curso_bloquea_otro_inicio() {
            var cliente = createTemporaryObject(compCliente, raiz);
            cliente.activa = true;
            var pagina = crear(cliente);
            compare(findChild(pagina, "botonIniciar").enabled, false);
        }
    }

    Component { id: spyComp; SignalSpy {} }
}
