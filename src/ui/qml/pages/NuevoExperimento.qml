import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../components/Tipografia.js" as Tipografia

// Configuración de una corrida. Los tipos de modelo y sus hiperparámetros salen del catálogo
// que entrega energia_orquestador (D-009): no hay formularios fijos por modelo.
Item {
    id: root

    property AppState appState
    // ClienteOrquestador (C++). Es null cuando la vista se abre sin backend (por ejemplo, con qml.exe).
    property var cliente: null

    signal experimentoIniciado()

    readonly property bool hayCliente: cliente !== null
    readonly property var modelos: hayCliente ? cliente.modelos : []
    property int indiceModelo: -1
    readonly property var modelo: indiceModelo >= 0 && indiceModelo < modelos.length ? modelos[indiceModelo] : null

    property string origenDatos: "csv"
    property string rutaCsv: ""
    property var encabezado: null          // { ok, columnas, separador, error }
    property int indiceColumna: -1
    property string mensajeError: ""
    // Monitor de software (system_meter) incluido en la corrida
    property bool incluirSistema: appState ? appState.softwareMonitorEnabled : true
    // Monitor fisico de energia (energia_ble.py): desactivado por defecto porque sin el Atorch
    // encendido la corrida se cancela en t_start
    property bool incluirEnergia: appState ? appState.energyMonitorEnabled : false
    readonly property var laboratorio: hayCliente && cliente.medidorEnergiaLaboratorio
                                       ? cliente.medidorEnergiaLaboratorio() : null

    readonly property bool hiperValidos: {
        for (var i = 0; i < hiperRepeater.count; i++) {
            var item = hiperRepeater.itemAt(i);
            if (!item || !item.valido) return false;
        }
        return true;
    }

    // Tiempo de la corrida sin el entrenamiento (que depende del modelo y los datos), en segundos:
    // margen de inicio (10 s con el monitor de energia, 2 s con el de software), las lineas base y,
    // con la inferencia (D-014), la linea base intermedia y la duracion minima de la inferencia
    function tiempoSinEntrenamientoS() {
        var margen = incluirEnergia ? 10 : incluirSistema ? 2 : 0;
        var t = margen + 2 * lineaBase.value;
        if (inferenciaActivada.checked) t += lineaBase.value + inferenciaDuracion.value;
        return t;
    }
    function textoDuracion(s) {
        var m = Math.floor(s / 60), r = s % 60;
        return m > 0 ? m + " min" + (r > 0 ? " " + r + " s" : "") : s + " s";
    }

    function joinParts(parts, separator, fallback) {
        var kept = parts.filter(function (p) { return p !== ""; });
        return kept.length ? kept.join(separator) : fallback;
    }

    function nombreArchivo(ruta) {
        var partes = ruta.split(/[\\/]/);
        return partes[partes.length - 1];
    }

    function seleccionarCsv(ruta) {
        rutaCsv = ruta;
        indiceColumna = -1;
        encabezado = hayCliente ? cliente.encabezadoCsv(ruta) : null;
        // Por convención la última columna suele ser la etiqueta; el usuario puede cambiarla
        if (encabezado && encabezado.ok && encabezado.columnas.length > 0)
            indiceColumna = encabezado.columnas.length - 1;
    }

    function recogerHiperparametros() {
        var h = {};
        for (var i = 0; i < hiperRepeater.count; i++) {
            var item = hiperRepeater.itemAt(i);
            h[item.clave] = item.valor;
        }
        return h;
    }

    function descripcionDatos() {
        if (origenDatos === "sintetico") return "Datos sintéticos de prueba (no es un dataset real)";
        if (rutaCsv === "") return "Sin seleccionar";
        var col = encabezado && encabezado.ok && indiceColumna >= 0 ? encabezado.columnas[indiceColumna] : "—";
        return nombreArchivo(rutaCsv) + " · objetivo: " + col;
    }

    // Lo que falta para poder iniciar (vacío = listo)
    function faltantes() {
        var f = [];
        if (!hayCliente) { f.push("El proceso de ejecución no está disponible en esta vista."); return f; }
        if (cliente.activa) f.push("Ya hay una corrida en curso.");
        if (modelo === null) f.push("Selecciona un tipo de modelo.");
        if (nombreExperimento.text.trim() === "") f.push("Indica un nombre para el experimento.");
        if (origenDatos === "csv") {
            if (rutaCsv === "") f.push("Selecciona el archivo CSV de datos.");
            else if (!encabezado || !encabezado.ok) f.push("El CSV no se pudo leer.");
            else if (indiceColumna < 0) f.push("Selecciona la columna objetivo.");
        }
        if (!hiperValidos) f.push("Revisa los hiperparámetros marcados en rojo.");
        if (!semilla.acceptableInput) f.push("La semilla debe ser un entero no negativo.");
        if (incluirEnergia && laboratorio && !laboratorio.python_existe)
            f.push("Falta el entorno de Python del monitor físico de energía (crear_entorno.bat).");
        return f;
    }

    function iniciar() {
        mensajeError = "";
        var datos = origenDatos === "sintetico"
            ? { origen: "sintetico" }
            : { origen: "csv", ruta: rutaCsv, columna_objetivo: encabezado.columnas[indiceColumna] };
        var seleccion = {
            experimento: nombreExperimento.text.trim(),
            modelo: modelo.ruta,
            modelo_nombre: modelo.nombre,
            hiperparametros: recogerHiperparametros(),
            datos: datos,
            datos_descripcion: descripcionDatos(),
            porcentaje_entrenamiento: porcentaje.value,
            semilla: parseInt(semilla.text, 10),
            guardar_modelo: guardarModelo.checked,
            linea_base_s: lineaBase.value,
            medidores: { sistema: incluirSistema, energia: incluirEnergia },
            inferencia: { activada: inferenciaActivada.checked, duracion_min_s: inferenciaDuracion.value,
                          lote: inferenciaLote.value }
        };
        var error = cliente.iniciar(seleccion);
        if (error !== "") mensajeError = error;
        else root.experimentoIniciado();
    }

    function ajustarSeleccion() {
        if (indiceModelo >= modelos.length) indiceModelo = -1;
        if (indiceModelo < 0 && modelos.length > 0) indiceModelo = 0;
    }

    onModelosChanged: ajustarSeleccion()
    Component.onCompleted: {
        if (hayCliente && cliente.modoDesarrollo) origenDatos = "sintetico";
        // Precargada con el valor de Configuracion; editable solo para esta corrida
        if (hayCliente) lineaBase.value = cliente.lineaBaseSegundos;
        ajustarSeleccion();
    }

    FileDialog {
        id: dialogoCsv
        title: "Selecciona el dataset (CSV)"
        nameFilters: ["Archivos CSV (*.csv)", "Todos los archivos (*)"]
        currentFolder: root.hayCliente && root.cliente.dirDatasets
                       ? root.cliente.urlCarpeta(root.cliente.dirDatasets) : ""
        onAccepted: root.seleccionarCsv(root.cliente.rutaLocal(selectedFile))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: mainRow.height + 40
            clip: true

            RowLayout {
                id: mainRow
                x: 24
                y: 20
                width: parent.width - 48
                spacing: 20

                // Columna izquierda: formulario
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    Layout.alignment: Qt.AlignTop
                    spacing: 20

                    // Aviso de modo de desarrollo
                    Rectangle {
                        visible: root.hayCliente && root.cliente.modoDesarrollo
                        Layout.fillWidth: true
                        implicitHeight: avisoDesarrollo.height + 20
                        radius: 10
                        color: "#2a2210"
                        border.color: "#f59e0b"
                        Text {
                            id: avisoDesarrollo
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 10
                            text: "Modo de desarrollo: se ofrecen la DLL de prueba interna y los datos sintéticos. No son modelos ni datos del estudio; no los uses para resultados de la tesis."
                            color: "#fbbf24"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap
                        }
                    }

                    // 1. Tipo de modelo
                    SectionCard {
                        Layout.fillWidth: true
                        title: "1. Tipo de modelo"
                        subtitle: "Tipos de modelo entrenables descubiertos en las DLL que cumplen el contrato v4"

                        Text {
                            visible: !root.hayCliente
                            text: "Vista sin proceso de ejecución: el catálogo de modelos no está disponible."
                            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Text {
                            visible: root.hayCliente && root.cliente.estadoCatalogo === "cargando"
                            text: "Buscando tipos de modelo…"
                            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                        }
                        RowLayout {
                            visible: root.hayCliente && root.cliente.estadoCatalogo === "error"
                            Layout.fillWidth: true
                            Text {
                                text: root.hayCliente ? root.cliente.errorCatalogo : ""
                                color: "#f87171"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                            Button { text: "Reintentar"; onClicked: root.cliente.cargarCatalogo() }
                        }
                        Text {
                            visible: root.hayCliente && root.cliente.estadoCatalogo === "listo" && root.modelos.length === 0
                            text: "No hay tipos de modelo disponibles en " + (root.hayCliente ? root.cliente.dirModelos : "")
                                  + ". Cada tipo de modelo (Árbol de decisión, MLP) se agrega como una DLL EnergIA_*.dll."
                                  + (root.hayCliente && root.cliente.modelosInternosOcultos > 0
                                     ? " La DLL de prueba interna está oculta; inicia la aplicación con --desarrollo para usarla." : "")
                            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }

                        Repeater {
                            id: modelosRepeater
                            model: root.modelos
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool elegido: index === root.indiceModelo
                                objectName: "modelo_" + modelData.clave
                                Layout.fillWidth: true
                                implicitHeight: filaModelo.height + 24
                                radius: 10
                                color: elegido ? "#122033" : "#0d1117"
                                border.color: elegido ? "#3b82f6" : "#30363d"

                                RowLayout {
                                    id: filaModelo
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 12
                                    spacing: 12

                                    Rectangle {
                                        implicitWidth: 16; implicitHeight: 16; radius: 8
                                        color: "transparent"; border.color: elegido ? "#3b82f6" : "#484f58"; border.width: 2
                                        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: "#3b82f6"; visible: elegido }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        RowLayout {
                                            spacing: 8
                                            Text { text: modelData.nombre; color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true }
                                            Rectangle {
                                                visible: modelData.interno
                                                radius: 6; color: "#2a2210"
                                                implicitWidth: internoTxt.width + 14; implicitHeight: 18
                                                Text { id: internoTxt; anchors.centerIn: parent; text: "Prueba interna · no es un modelo del estudio"; color: "#fbbf24"; font.pixelSize: Tipografia.detalle }
                                            }
                                        }
                                        Text {
                                            text: "Librería: " + modelData.libreria + " · unidad de paso: " + modelData.unidad_paso
                                                  + " · hilos automáticos: " + modelData.hilos + " · " + modelData.archivo
                                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                        }
                                    }
                                }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.indiceModelo = index }
                            }
                        }
                    }

                    // 2. Datos de entrenamiento
                    SectionCard {
                        Layout.fillWidth: true
                        title: "2. Datos de entrenamiento"
                        subtitle: "CSV numérico con encabezado; la columna objetivo contiene la clase"

                        RowLayout {
                            visible: root.hayCliente && root.cliente.modoDesarrollo
                            spacing: 16
                            RadioButton {
                                text: "Archivo CSV"
                                checked: root.origenDatos === "csv"
                                onClicked: root.origenDatos = "csv"
                            }
                            RadioButton {
                                text: "Datos sintéticos de prueba"
                                checked: root.origenDatos === "sintetico"
                                onClicked: root.origenDatos = "sintetico"
                            }
                        }

                        ColumnLayout {
                            visible: root.origenDatos === "csv"
                            Layout.fillWidth: true
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Button {
                                    text: root.rutaCsv === "" ? "Seleccionar CSV…" : "Cambiar CSV…"
                                    enabled: root.hayCliente
                                    onClicked: dialogoCsv.open()
                                }
                                Text {
                                    text: root.rutaCsv === "" ? "Ningún archivo seleccionado" : root.rutaCsv
                                    color: root.rutaCsv === "" ? "#484f58" : "#c9d1d9"
                                    font.pixelSize: Tipografia.cuerpo; elide: Text.ElideMiddle; Layout.fillWidth: true
                                }
                            }
                            Text {
                                visible: root.encabezado !== null && !root.encabezado.ok
                                text: root.encabezado ? "No se pudo leer el encabezado: " + root.encabezado.error : ""
                                color: "#f87171"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: root.encabezado !== null && root.encabezado.ok
                                Layout.fillWidth: true
                                spacing: 12
                                Text { text: "Columna objetivo"; color: "#8b949e"; font.pixelSize: Tipografia.cuerpo }
                                ComboBox {
                                    id: columnaObjetivo
                                    Layout.preferredWidth: 220
                                    model: root.encabezado && root.encabezado.ok ? root.encabezado.columnas : []
                                    currentIndex: root.indiceColumna
                                    onActivated: (i) => root.indiceColumna = i
                                }
                                Text {
                                    text: root.encabezado && root.encabezado.ok
                                          ? root.encabezado.columnas.length + " columnas · separador " + root.encabezado.separador : ""
                                    color: "#484f58"; font.pixelSize: Tipografia.auxiliar
                                }
                            }
                        }
                        Text {
                            visible: root.origenDatos === "sintetico"
                            text: "Se generan datos sintéticos reproducibles a partir de la semilla. Sirven para probar el flujo; no son un dataset del estudio."
                            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }

                    // 3. Hiperparámetros y partición
                    SectionCard {
                        Layout.fillWidth: true
                        title: "3. Hiperparámetros y partición"
                        subtitle: root.modelo ? "Definidos por la DLL de " + root.modelo.nombre : "Selecciona un tipo de modelo"

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 14

                            Repeater {
                                id: hiperRepeater
                                model: root.modelo ? root.modelo.hiperparametros : []
                                delegate: HiperparametroCampo {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    descriptor: modelData
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 16
                            rowSpacing: 6

                            Text { text: "Nombre del experimento"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            Text { text: "Entrenamiento (%)"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            Text { text: "Semilla de la partición"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }

                            TextField {
                                id: nombreExperimento
                                objectName: "nombreExperimento"
                                Layout.fillWidth: true
                                text: "experimento"
                                maximumLength: 60
                                selectByMouse: true
                            }
                            SpinBox {
                                id: porcentaje
                                objectName: "porcentaje"
                                from: 50; to: 95; value: 80
                                editable: true
                            }
                            TextField {
                                id: semilla
                                objectName: "semilla"
                                Layout.fillWidth: true
                                text: "42"
                                validator: IntValidator { bottom: 0; top: 2147483647 }
                                selectByMouse: true
                            }
                        }
                        Text {
                            text: "El resto de las filas se usa para la evaluación en validación, fuera de la ventana de medición."
                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Casilla {
                            id: guardarModelo
                            objectName: "guardarModelo"
                            text: "Guardar el modelo entrenado como artefacto de la corrida"
                        }
                        Text {
                            text: "El artefacto se conserva en la carpeta de la corrida, pero EnergIA no puede volver a cargarlo (contrato v4, D-009)."
                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }

                    // 4. Medición
                    SectionCard {
                        Layout.fillWidth: true
                        title: "4. Medición"
                        subtitle: "Rejilla temporal común de 1 s (D-007)"

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width >= 650 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 12

                            MetricGroupSelector {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                title: "Uso de recursos computacionales"
                                monitorName: "Monitor de software"
                                accent: root.appState.resourcesAccent
                                metrics: root.appState.resourceMetrics
                                showToggle: true
                                included: root.incluirSistema
                                onToggled: (incluido) => root.incluirSistema = incluido
                                objectName: "selectorRecursos"
                            }
                            MetricGroupSelector {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                title: "Métricas de energía"
                                monitorName: "Monitor físico de energía"
                                accent: root.appState.energyAccent
                                metrics: root.appState.energyMetrics
                                showToggle: true
                                included: root.incluirEnergia
                                onToggled: (incluido) => root.incluirEnergia = incluido
                                objectName: "selectorEnergia"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text { text: "Línea base (s)"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            SpinBox {
                                id: lineaBase
                                objectName: "lineaBase"
                                from: 0; to: 3600; value: 20
                                editable: true
                            }
                            Text {
                                text: "Reposo medido antes y después del entrenamiento (las dos fases). Por defecto, el valor de Configuración."
                                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                        Text {
                            text: root.incluirSistema
                                  ? "El monitor de software mide CPU y RAM del proceso de ejecución y del sistema sobre la rejilla de 1 s."
                                  : "Sin monitor de software: esta corrida no registrará CPU ni RAM."
                            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Text {
                            text: root.incluirEnergia
                                  ? "El monitor físico de energía necesita el Atorch S1BW encendido y al alcance por Bluetooth"
                                    + (root.laboratorio && root.laboratorio.mac ? " (MAC " + root.laboratorio.mac + ")" : "")
                                    + ". Si no confirma una trama antes de t_start (margen de 10 s), la corrida se cancela sin entrenar. "
                                    + "La energía del experimento se calcula al terminar, integrando la potencia (D-008)."
                                  : "Sin monitor físico de energía: esta corrida no registrará energía."
                            color: root.incluirEnergia ? "#8b949e" : "#fbbf24"
                            font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }

                    // 5. Inferencia (D-014): opcional por corrida
                    SectionCard {
                        objectName: "seccionInferencia"
                        Layout.fillWidth: true
                        title: "5. Inferencia"
                        subtitle: "Opcional: mide también las predicciones del modelo entrenado"

                        Casilla {
                            id: inferenciaActivada
                            objectName: "inferenciaActivada"
                            text: "Medir la inferencia después del entrenamiento"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            enabled: inferenciaActivada.checked
                            Text { text: "Duración mínima (s)"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            SpinBox {
                                id: inferenciaDuracion
                                objectName: "inferenciaDuracion"
                                from: 1; to: 3600; value: 60
                                editable: true
                            }
                            Text {
                                text: "La inferencia repite pasadas completas por los datos hasta cumplir este tiempo (puede durar hasta ~1 s más)."
                                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                        Text {
                            text: "Predice sobre la partición de validación (" + (100 - porcentaje.value) + " % de las filas). Antes de la inferencia "
                                  + "se mide una línea base intermedia con la misma duración que la línea base ("
                                  + lineaBase.value + " s). Las predicciones se descartan: no se calcula ninguna métrica de calidad durante la medición."
                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                        Button {
                            id: botonAvanzadoInferencia
                            objectName: "botonAvanzadoInferencia"
                            property bool abierto: false
                            enabled: inferenciaActivada.checked
                            flat: true
                            text: (abierto ? "" : "") + " Avanzado"
                            onClicked: abierto = !abierto
                        }
                        RowLayout {
                            objectName: "avanzadoInferencia"
                            visible: botonAvanzadoInferencia.abierto
                            enabled: inferenciaActivada.checked
                            Layout.fillWidth: true
                            spacing: 12
                            Text { text: "Lote de inferencia (filas)"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            SpinBox {
                                id: inferenciaLote
                                objectName: "inferenciaLote"
                                from: 1; to: 1000000; value: 65536
                                editable: true
                            }
                            Text {
                                text: "Filas por bloque de predicción. 65.536 es la forma habitual (todos los datos de una vez, en bloques); con 1 se mide predecir consulta por consulta."
                                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                    }
                }

                // Columna derecha: resumen
                Rectangle {
                    Layout.preferredWidth: 280
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: summaryCol.height + 40
                    radius: 12
                    color: "#161b22"
                    border.color: "#21262d"

                    ColumnLayout {
                        id: summaryCol
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 20
                        spacing: 18

                        Text { text: "Resumen del experimento"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloSeccion; font.bold: true }

                        Text {
                            objectName: "grupoEntradaNuevo"
                            text: "DATOS DE ENTRADA"
                            color: "#60a5fa"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                        }
                        SummaryItem {
                            icon: "modelo"; title: "Tipo de modelo"
                            description: root.modelo ? root.modelo.nombre + " · " + root.modelo.hiperparametros.length + " hiperparámetros" : "Sin seleccionar"
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            icon: "archivo"; title: "Datos"
                            description: root.descripcionDatos() + " · " + porcentaje.value + "% entrenamiento"
                            Layout.fillWidth: true
                        }
                        Text {
                            objectName: "grupoExperimentoNuevo"
                            text: "DATOS DEL EXPERIMENTO"
                            color: "#60a5fa"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                        }
                        SummaryItem {
                            icon: "reloj"; title: "Rejilla temporal"
                            description: "1 s, alineada a t_start (D-007)"
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            objectName: "resumenLineaBase"
                            icon: "linea_base"; title: "Línea base"
                            description: lineaBase.value > 0 ? lineaBase.value + " s antes y " + lineaBase.value + " s después del entrenamiento"
                                                            : "Sin línea base"
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            objectName: "resumenInferencia"
                            icon: "modelo"; title: "Inferencia"
                            description: inferenciaActivada.checked
                                         ? "Al menos " + inferenciaDuracion.value + " s · lote de " + inferenciaLote.value + " filas · partición de validación"
                                         : "Sin inferencia"
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            objectName: "resumenTiempoTotal"
                            icon: "reloj"; title: "Tiempo total estimado"
                            description: "≈ " + root.textoDuracion(root.tiempoSinEntrenamientoS()) + " más lo que tarde el entrenamiento"
                            Layout.fillWidth: true
                        }
                        Text {
                            objectName: "grupoSalidaNuevo"
                            text: "DATOS DE SALIDA"
                            color: "#4ade80"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                        }
                        SummaryItem {
                            icon: "monitor"; title: "Monitores"
                            description: root.joinParts([root.incluirSistema ? "Monitor de software (CPU, RAM)" : "",
                                                         root.incluirEnergia ? "Monitor físico de energía" : ""],
                                                        " + ", "Ningún monitor seleccionado")
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            objectName: "resultadoPrevisto"
                            icon: "barras"; title: "Resultados"
                            description: "Al finalizar: evaluación del modelo y mediciones de los monitores seleccionados."
                            Layout.fillWidth: true
                        }
                        SummaryItem {
                            icon: "carpeta"; title: "Carpeta de salida"
                            description: root.hayCliente ? root.cliente.dirSalida : "—"
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            id: cajaListo
                            objectName: "cajaListo"
                            readonly property var pendientes: root.faltantes()
                            Layout.fillWidth: true
                            implicitHeight: readyBox.height + 24
                            radius: 10
                            color: "#0d1117"
                            border.color: pendientes.length === 0 ? "#22c55e" : "#f59e0b"

                            ColumnLayout {
                                id: readyBox
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 12
                                spacing: 4

                                RowLayout {
                                    spacing: 6
                                    Icono {
                                        nombre: cajaListo.pendientes.length === 0 ? "check" : "alerta"
                                        tamano: 14
                                        grosor: 2
                                        color: cajaListo.pendientes.length === 0 ? "#22c55e" : "#f59e0b"
                                    }
                                    Text {
                                        text: cajaListo.pendientes.length === 0 ? "Listo para ejecutar" : "Falta completar"
                                        color: cajaListo.pendientes.length === 0 ? "#22c55e" : "#f59e0b"
                                        font.pixelSize: Tipografia.destacado; font.bold: true
                                    }
                                }
                                Repeater {
                                    model: cajaListo.pendientes
                                    delegate: Text {
                                        required property string modelData
                                        text: "• " + modelData
                                        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        Text {
                            visible: root.mensajeError !== ""
                            text: root.mensajeError
                            color: "#f87171"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // Barra inferior de acciones
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "#0d1117"
            border.color: "#21262d"

            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 24
                spacing: 12

                Item { Layout.fillWidth: true }

                BotonIcono {
                    id: botonIniciar
                    objectName: "botonIniciar"
                    icono: "reproducir"
                    text: "Iniciar experimento"
                    enabled: cajaListo.pendientes.length === 0
                    implicitHeight: 34
                    implicitWidth: 170
                    font.pixelSize: Tipografia.destacado
                    font.bold: true
                    colorContenido: "#0d1117"
                    background: Rectangle { radius: 8; color: botonIniciar.enabled ? "#22c55e" : "#1f3b2a" }
                    onClicked: root.iniciar()
                }
            }
        }
    }
}
