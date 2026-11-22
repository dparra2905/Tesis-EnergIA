import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/Ayudas.js" as Ayudas
import "../components/Tipografia.js" as Tipografia

// Corrida en curso o recién terminada, según los eventos de energia_orquestador (D-010).
// Las marcas temporales de la corrida las toma el orquestador; aquí solo se muestran.
Flickable {
    id: root
    contentWidth: width
    contentHeight: (hayCorrida ? mainRow.height : vacio.height) + 40
    clip: true

    property AppState appState
    // ClienteOrquestador (C++). Es null cuando la vista se abre sin backend.
    property var cliente: null
    // Cola de subidas (ColaSubidas) e historial (Historial): decisión de subida y nota (D-012)
    property var subidas: null
    property var historial: null

    signal nuevoExperimentoSolicitado()

    readonly property bool hayCliente: cliente !== null
    readonly property string estado: hayCliente ? cliente.estado : "inactivo"
    readonly property bool hayCorrida: estado !== "inactivo"
    readonly property bool terminada: estado === "completada" || estado === "cancelada" || estado === "error"
    readonly property var corrida: hayCliente ? cliente.corrida : ({})
    readonly property var error: corrida && corrida.error ? corrida.error : null
    // Auditoría de la corrida (evento 'auditoria', D-012) y si la corrida espera decisión
    readonly property var auditoria: corrida && corrida.auditoria ? corrida.auditoria : null
    // Inferencia (D-014) activada en esta corrida
    readonly property bool conInferencia: corrida !== undefined && corrida.inferencia !== undefined && corrida.inferencia.activada === true
    readonly property bool esperaDecision: {
        var x = subidas ? subidas.elementos : [];
        return subidas !== null && terminada && !!corrida.dir_corrida && subidas.estadoDe(corrida.dir_corrida).estado === "esperando_decision";
    }
    function problemasAuditoria() {
        if (!auditoria || !auditoria.comprobaciones) return [];
        return auditoria.comprobaciones.filter(function (c) { return c.resultado === "advertencia" || c.resultado === "no_valida"; });
    }

    // Instante de referencia para la duración mostrada (reloj de la interfaz, solo informativo)
    property double ahora: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: root.hayCorrida && !root.terminada
        onTriggered: root.ahora = Date.now()
    }

    function textoEstado(e) {
        switch (e) {
        case "iniciando": return "Iniciando";
        case "en_curso": return "En ejecución";
        case "cancelando": return "Cancelando";
        case "completada": return "Completada";
        case "cancelada": return "Cancelada";
        case "error": return "Error";
        default: return "Sin corrida";
        }
    }
    function colorEstado(e) {
        switch (e) {
        case "en_curso": case "iniciando": return "#3b82f6";
        case "cancelando": case "cancelada": return "#f59e0b";
        case "completada": return "#22c55e";
        case "error": return "#f87171";
        default: return "#8b949e";
        }
    }
    function colorRegistro(tipo) {
        switch (tipo) {
        case "success": return "#22c55e";
        case "warning": return "#f59e0b";
        case "error": return "#f87171";
        default: return "#3b82f6";
        }
    }
    // Monitores de la corrida (F5): estado según los eventos 'medidor' del orquestador
    readonly property bool hayMetricas: hayCliente && cliente.metricasSistema !== undefined
                                        && Object.keys(cliente.metricasSistema).length > 0
    function estadoMedidor(nombre) {
        if (!hayCliente || !cliente.medidoresCorrida) return "";
        for (var i = 0; i < cliente.medidoresCorrida.length; i++)
            if (cliente.medidoresCorrida[i].nombre === nombre) return cliente.medidoresCorrida[i].estado;
        return "";
    }
    function textoMedidor(e) {
        switch (e) {
        case "pendiente": return "Preparando";
        case "lanzado": return "Midiendo";
        case "terminado": return "Terminado";
        case "termino_antes": return "Terminó antes de tiempo";
        case "forzado": return "Cerrado a la fuerza";
        default: return "No incluido";
        }
    }
    function colorMedidor(e) {
        switch (e) {
        case "lanzado": return "#22c55e";
        case "pendiente": case "terminado": return "#3b82f6";
        case "termino_antes": case "forzado": return "#f59e0b";
        default: return "#8b949e";
        }
    }
    readonly property bool hayEnergia: hayCliente && cliente.metricasEnergia !== undefined
                                       && Object.keys(cliente.metricasEnergia).length > 0
    function valorEnergia(clave, decimales) {
        if (!hayEnergia) return "—";
        var v = cliente.metricasEnergia[clave];
        return v === undefined ? "—" : Number(v).toFixed(decimales);
    }
    // Energía del experimento (D-008): solo existe al terminar la corrida (resultados.json)
    function ventanaEntrenamiento() {
        return corrida && corrida.energia_experimento && corrida.energia_experimento.entrenamiento
               ? corrida.energia_experimento.entrenamiento : null;
    }
    function energiaExperimentoWh() {
        var v = ventanaEntrenamiento();
        return v ? v.energia_wh : null;
    }
    function textoEnergiaExperimento() {
        var wh = energiaExperimentoWh();
        return wh === null ? "Al terminar" : Number(wh).toFixed(4);
    }
    function detalleEnergiaExperimento() {
        var v = ventanaEntrenamiento();
        if (!v) return "";
        return "Integración de la potencia durante el entrenamiento (t_antes a t_despues, " + Number(v.duracion_s).toFixed(1)
               + " s): cobertura " + (100 * v.cobertura).toFixed(1) + " %, " + v.huecos.length
               + " hueco(s) sin interpolar. Dato derivado, sin corregir por la línea base; su validez está pendiente (D-008).";
    }
    function valorMetrica(clave, decimales) {
        if (!hayMetricas) return "—";
        var v = cliente.metricasSistema[clave];
        return v === undefined ? "—" : Number(v).toFixed(decimales);
    }

    function dosDigitos(n) { return n < 10 ? "0" + n : "" + n; }
    function duracion() {
        if (!corrida || !corrida.t_start) return "—";
        var inicio = Date.parse(corrida.t_start);
        var fin = root.terminada && corrida.t_fin ? Date.parse(corrida.t_fin) : root.ahora;
        if (isNaN(inicio) || isNaN(fin)) return "—";
        var s = Math.max(0, Math.floor((fin - inicio) / 1000));
        return dosDigitos(Math.floor(s / 3600)) + ":" + dosDigitos(Math.floor(s / 60) % 60) + ":" + dosDigitos(s % 60);
    }
    function joinParts(partes, separador, vacio) {
        var valores = partes.filter(function (parte) { return parte !== ""; });
        return valores.length ? valores.join(separador) : vacio;
    }
    function textoMetricas(m) {
        var partes = [];
        for (var k in m) partes.push(k + " = " + Number(m[k]).toPrecision(4));
        return partes.join(" · ");
    }

    // Sin corrida
    ColumnLayout {
        id: vacio
        visible: !root.hayCorrida
        x: 24; y: 20
        width: parent.width - 48
        spacing: 12

        SectionCard {
            Layout.fillWidth: true
            title: "No hay ningún experimento en ejecución"
            subtitle: root.hayCliente ? "Configura y lanza una corrida desde «Nuevo experimento»."
                                      : "Vista sin proceso de ejecución: no hay datos de corridas."
            Button {
                text: "Configurar un experimento"
                onClicked: root.nuevoExperimentoSolicitado()
            }
        }
    }

    RowLayout {
        id: mainRow
        visible: root.hayCorrida
        x: 24
        y: 20
        width: parent.width - 48
        spacing: 20

        // Columna izquierda
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            Layout.alignment: Qt.AlignTop
            spacing: 20

            // Encabezado de la corrida
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: infoRow.height + 32
                radius: 12; color: "#161b22"; border.color: "#21262d"

                RowLayout {
                    id: infoRow
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 14

                    Rectangle { implicitWidth: 38; implicitHeight: 38; radius: 9; color: "#1c2333"; Icono { anchors.centerIn: parent; nombre: "modelo"; tamano: 20; color: "#8b949e" } }

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text {
                            text: (root.corrida.modelo_nombre || "Modelo") + (root.corrida.modelo_interno ? "  ·  prueba interna" : "")
                            color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true
                        }
                        Text {
                            text: (root.corrida.experimento || "") + (root.corrida.corrida ? " · corrida_" + ("00" + root.corrida.corrida).slice(-3) : "")
                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                        }
                    }

                    Rectangle {
                        objectName: "estadoCorrida"
                        radius: 8; color: "#0d1117"; border.color: root.colorEstado(root.estado)
                        implicitWidth: filaEstado.width + 18; implicitHeight: 22
                        Row {
                            id: filaEstado
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { width: 6; height: 6; radius: 3; color: root.colorEstado(root.estado); anchors.verticalCenter: parent.verticalCenter }
                            Text { text: root.textoEstado(root.estado); color: root.colorEstado(root.estado); font.pixelSize: Tipografia.auxiliar; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        Text { text: "Duración desde t_start"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.alignment: Qt.AlignRight }
                        Text { text: root.duracion(); color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true; Layout.alignment: Qt.AlignRight }
                    }
                }
            }

            // Error
            Rectangle {
                visible: root.estado === "error" && root.error !== null
                Layout.fillWidth: true
                implicitHeight: errorCol.height + 24
                radius: 12; color: "#2a1215"; border.color: "#f87171"
                ColumnLayout {
                    id: errorCol
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 12
                    spacing: 4
                    Text { text: "La corrida no terminó correctamente"; color: "#f87171"; font.pixelSize: Tipografia.destacado; font.bold: true }
                    Text {
                        objectName: "mensajeError"
                        text: root.error ? root.error.mensaje : ""
                        color: "#fca5a5"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }

            // Progreso del entrenamiento
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: progCol.height + 32
                radius: 12; color: "#161b22"; border.color: "#21262d"

                ColumnLayout {
                    id: progCol
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Progreso del entrenamiento"; color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true; Layout.fillWidth: true }
                        Text {
                            objectName: "textoProgreso"
                            text: root.hayCliente && root.cliente.totalPasos > 0
                                  ? root.cliente.pasosCompletados + " / " + root.cliente.totalPasos + " · " + root.cliente.unidadPaso
                                  : "Sin pasos todavía"
                            color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                        }
                        Rectangle {
                            visible: root.hayCliente && root.cliente.fase !== ""
                            radius: 8; color: "#1c2333"
                            implicitWidth: faseTxt.width + 16; implicitHeight: 20
                            Text { id: faseTxt; anchors.centerIn: parent; text: root.hayCliente ? root.cliente.textoFase(root.cliente.fase) : ""; color: "#3b82f6"; font.pixelSize: Tipografia.detalle }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 8; radius: 4; color: "#21262d"
                        Rectangle {
                            width: root.hayCliente && root.cliente.totalPasos > 0
                                   ? parent.width * root.cliente.pasosCompletados / root.cliente.totalPasos : 0
                            height: parent.height; radius: 4; color: root.colorEstado(root.estado)
                        }
                    }

                    Text {
                        visible: root.hayCliente && Object.keys(root.cliente.metricasPaso).length > 0
                        text: root.hayCliente ? "Último paso: " + root.textoMetricas(root.cliente.metricasPaso) : ""
                        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                    }
                    // Inferencia (D-014): grupo, pasada y predicciones acumuladas
                    Text {
                        objectName: "textoInferencia"
                        visible: root.conInferencia
                        text: {
                            if (!root.hayCliente) return "";
                            var p = root.cliente.progresoInferencia;
                            var inf = root.corrida.inferencia || {};
                            if (p && p.pasadas !== undefined)
                                return "Inferencia: grupo " + p.grupos + " · pasada " + p.pasadas + " · " + p.predicciones + " predicciones"
                                       + (inf.status_texto ? " · " + inf.status_texto : "");
                            if (root.cliente.fase === "fase_media") return "Inferencia: línea base intermedia en curso";
                            return "Inferencia: al menos " + inf.duracion_min_s + " s después del entrenamiento (lote de " + inf.lote + " filas)";
                        }
                        color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                    }
                    Text {
                        visible: root.corrida.evaluacion !== undefined && root.corrida.evaluacion.ejecutada === true
                        text: root.corrida.evaluacion ? "Evaluación en validación: " + root.textoMetricas(root.corrida.evaluacion.metricas || {}) : ""
                        color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                    }
                }
            }

            // Auditoría al terminar (D-012): informa; no bloquea. Si el modo de subida pide
            // decisión, las acciones van aquí mismo.
            Rectangle {
                objectName: "tarjetaAuditoria"
                visible: root.terminada && root.auditoria !== null
                Layout.fillWidth: true
                implicitHeight: audCol.height + 32
                radius: 12; color: "#161b22"; border.color: "#21262d"

                ColumnLayout {
                    id: audCol
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Auditoría de la corrida"; color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true; Layout.fillWidth: true }
                        Veredicto { objectName: "veredictoCorrida"; veredicto: root.auditoria ? root.auditoria.veredicto : "" }
                    }
                    Text {
                        visible: root.problemasAuditoria().length === 0
                        text: "Todas las comprobaciones evaluables dieron ok."
                        color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                    }
                    Repeater {
                        model: root.problemasAuditoria()
                        delegate: Text {
                            required property var modelData
                            text: "• " + modelData.id + ": " + modelData.motivo
                            color: modelData.resultado === "no_valida" ? "#f87171" : "#fbbf24"
                            font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                    Text {
                        text: "La auditoría informa; no bloquea la corrida ni la subida. Umbrales provisionales (reglas v"
                              + (root.auditoria ? root.auditoria.version_reglas : "") + "). El detalle completo está en Resultados."
                        color: "#484f58"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Text {
                        visible: root.esperaDecision
                        text: "Por el modo de subida, esta corrida espera tu decisión antes de subirse a la base de datos."
                        color: "#f59e0b"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        objectName: "accionesDecisionCorrida"
                        visible: root.esperaDecision
                        spacing: 8
                        Button {
                            objectName: "botonDecisionSubir"; text: "Subir"
                            enabled: !(root.appState && root.appState.corridaActiva)
                            onClicked: root.subidas.decidir(root.corrida.dir_corrida, true)
                        }
                        Button {
                            objectName: "botonDecisionNota"
                            text: "Subir con nota"
                            enabled: !(root.appState && root.appState.corridaActiva)
                            onClicked: {
                                notaDecision.texto = root.historial ? root.historial.nota(root.corrida.dir_corrida) : "";
                                notaDecision.open();
                            }
                        }
                        Button { objectName: "botonDecisionNoSubir"; text: "No subir"; onClicked: root.subidas.decidir(root.corrida.dir_corrida, false) }
                    }
                    Text {
                        visible: root.esperaDecision && root.appState !== null && root.appState.corridaActiva
                        text: root.appState ? root.appState.avisoCorridaActiva : ""
                        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }

            // Métricas en tiempo real: grupos separados por monitor (D-002). Sin fuente real todavía.
            SectionCard {
                objectName: "tarjetaRecursos"
                Layout.fillWidth: true
                title: "Uso de recursos computacionales"
                subtitle: "Proceso de ejecución (orquestador y DLL) y sistema completo · rejilla de 1 s"
                accent: root.appState.resourcesAccent
                ayuda: Ayudas.texto("grupo_recursos")
                badgeText: "Monitor de software · " + root.textoMedidor(root.estadoMedidor("sistema"))
                badgeColor: root.colorMedidor(root.estadoMedidor("sistema"))

                Text {
                    visible: root.estadoMedidor("sistema") === ""
                    text: "Sin monitor de software en esta corrida: no se registran CPU ni RAM."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    visible: root.estadoMedidor("sistema") !== "" && !root.hayMetricas
                    text: "Esperando la primera lectura: el monitor mide a partir de t_start, una fila por segundo."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                GridLayout {
                    visible: root.hayMetricas
                    Layout.fillWidth: true
                    columns: width >= 520 ? 2 : 1
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        model: [
                            { clave: "cpu_proceso_pct", etiqueta: "CPU · proceso de ejecución", unidad: "% del equipo", icono: "cpu", color: "#3b82f6", decimales: 1 },
                            { clave: "cpu_sistema_pct", etiqueta: "CPU · sistema", unidad: "%", icono: "cpu", color: "#60a5fa", decimales: 1 },
                            { clave: "ram_proceso_mb", etiqueta: "RAM · proceso de ejecución", unidad: "MB", icono: "memoria", color: "#a78bfa", decimales: 0 },
                            { clave: "ram_sistema_pct", etiqueta: "RAM · sistema", unidad: "%", icono: "memoria", color: "#c4b5fd", decimales: 1 }
                        ]
                        delegate: LiveMetricCard {
                            required property var modelData
                            objectName: "metrica_" + modelData.clave
                            Layout.fillWidth: true
                            icon: modelData.icono; label: modelData.etiqueta; tint: modelData.color; unit: modelData.unidad
                            ayuda: Ayudas.texto(modelData.clave)
                            value: root.valorMetrica(modelData.clave, modelData.decimales)
                            values: root.hayCliente && root.cliente.historialSistema[modelData.clave]
                                    ? root.cliente.historialSistema[modelData.clave] : []
                        }
                    }
                }
            }

            SectionCard {
                objectName: "tarjetaGraficaRam"
                Layout.fillWidth: true
                visible: root.estadoMedidor("sistema") !== ""
                title: "Evolución de la memoria RAM"
                subtitle: "Monitor de software · hasta 120 lecturas recientes, una por segundo"
                accent: "#a78bfa"

                Text {
                    visible: !root.hayCliente || !root.cliente.historialSistema.ram_proceso_mb
                             || root.cliente.historialSistema.ram_proceso_mb.length < 2
                    text: "Esperando al menos dos mediciones de RAM del proceso."
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                Text {
                    text: "RAM utilizada por el proceso de ejecución (MB)"
                    color: "#a78bfa"; font.pixelSize: Tipografia.cuerpo; font.bold: true
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                LineChart {
                    objectName: "graficaRamProceso"
                    Layout.fillWidth: true
                    implicitHeight: 172
                    unidad: "MB"
                    series: root.hayCliente && root.cliente.historialSistema.ram_proceso_mb
                            ? [{ name: "RAM del proceso", color: "#a78bfa",
                                 values: root.cliente.historialSistema.ram_proceso_mb }] : []
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                Text {
                    text: "Memoria RAM ocupada en todo el sistema (%)"
                    color: "#c4b5fd"; font.pixelSize: Tipografia.cuerpo; font.bold: true
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                LineChart {
                    objectName: "graficaRamSistema"
                    Layout.fillWidth: true
                    implicitHeight: 172
                    unidad: "%"
                    series: root.hayCliente && root.cliente.historialSistema.ram_sistema_pct
                            ? [{ name: "RAM del sistema", color: "#c4b5fd",
                                 values: root.cliente.historialSistema.ram_sistema_pct }] : []
                }
                Text {
                    text: "Son dos medidas diferentes: MB del proceso y porcentaje total del equipo. Los huecos sin datos no se interpolan."
                    color: "#8b949e"; font.pixelSize: Tipografia.detalle
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
            }

            SectionCard {
                objectName: "tarjetaEnergia"
                Layout.fillWidth: true
                title: "Métricas de energía"
                subtitle: "Consumo físico del equipo (Atorch S1BW) · rejilla de 1 s"
                accent: root.appState.energyAccent
                ayuda: Ayudas.texto("grupo_energia")
                badgeText: "Monitor físico de energía · " + root.textoMedidor(root.estadoMedidor("energia"))
                badgeColor: root.colorMedidor(root.estadoMedidor("energia"))

                Text {
                    visible: root.estadoMedidor("energia") === ""
                    text: "Sin monitor físico de energía en esta corrida: no se registra energía."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    objectName: "avisoConexionEnergia"
                    visible: root.estadoMedidor("energia") !== "" && root.hayEnergia && !root.cliente.metricasEnergia.conectado
                    text: "Sin conexión con el medidor en la última lectura: esas filas quedan vacías (no se interpolan)."
                    color: "#fbbf24"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                GridLayout {
                    visible: root.estadoMedidor("energia") !== ""
                    Layout.fillWidth: true
                    columns: width >= 520 ? 2 : 1
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        model: [
                            { clave: "potencia_w", etiqueta: "Potencia", unidad: "W", icono: "enchufe", color: "#f59e0b", decimales: 1 },
                            { clave: "energia", etiqueta: "Energía del experimento", unidad: "Wh", icono: "rayo", color: "#22c55e", decimales: 4 },
                            { clave: "voltaje_v", etiqueta: "Voltaje", unidad: "V", icono: "voltaje", color: "#22d3ee", decimales: 1 },
                            { clave: "corriente_a", etiqueta: "Corriente", unidad: "A", icono: "corriente", color: "#f472b6", decimales: 3 }
                        ]
                        delegate: LiveMetricCard {
                            required property var modelData
                            objectName: "metrica_" + modelData.clave
                            Layout.fillWidth: true
                            icon: modelData.icono; label: modelData.etiqueta; tint: modelData.color
                            ayuda: Ayudas.texto(modelData.clave)
                            unit: modelData.clave === "energia" && root.energiaExperimentoWh() === null ? "" : modelData.unidad
                            value: modelData.clave === "energia" ? root.textoEnergiaExperimento()
                                                                 : root.valorEnergia(modelData.clave, modelData.decimales)
                            values: modelData.clave !== "energia" && root.hayCliente && root.cliente.historialEnergia[modelData.clave]
                                    ? root.cliente.historialEnergia[modelData.clave] : []
                        }
                    }
                }
                Text {
                    visible: root.energiaExperimentoWh() !== null
                    text: root.detalleEnergiaExperimento()
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

            // Registro de eventos
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: eventsCol.height + 32
                radius: 12; color: "#161b22"; border.color: "#21262d"

                ColumnLayout {
                    id: eventsCol
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 10

                    Text { text: "Registro de eventos"; color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true }

                    Repeater {
                        objectName: "registroEventos"
                        model: root.hayCliente ? root.cliente.registro : []
                        delegate: EventLogItem {
                            required property var modelData
                            Layout.fillWidth: true
                            time: modelData.hora
                            message: modelData.mensaje
                            dotColor: root.colorRegistro(modelData.tipo)
                        }
                    }
                }
            }
        }

        // Columna derecha
        ColumnLayout {
            Layout.preferredWidth: 280
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignTop
            spacing: 20

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: summaryCol.height + 40
                radius: 12; color: "#161b22"; border.color: "#21262d"

                ColumnLayout {
                    id: summaryCol
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 20
                    spacing: 18

                    Text { text: "Resumen de la corrida"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloSeccion; font.bold: true }

                    Text {
                        objectName: "grupoEntradaEjecucion"; text: "DATOS DE ENTRADA"
                        color: "#60a5fa"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                    }
                    SummaryItem { icon: "modelo"; title: "Tipo de modelo"; description: root.corrida.modelo_nombre || "—"; Layout.fillWidth: true }
                    SummaryItem { icon: "archivo"; title: "Datos"; description: root.corrida.datos_descripcion || "—"; Layout.fillWidth: true }
                    Text {
                        objectName: "grupoExperimentoEjecucion"; text: "DATOS DEL EXPERIMENTO"
                        color: "#60a5fa"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                    }
                    SummaryItem { icon: "reloj"; title: "Rejilla temporal"; description: "1 s, alineada a t_start (D-007)"; Layout.fillWidth: true }
                    SummaryItem {
                        objectName: "resumenLineaBase"
                        icon: "linea_base"; title: "Línea base"
                        description: root.corrida.linea_base_s === undefined ? "—"
                                     : root.corrida.linea_base_s > 0
                                       ? (root.conInferencia ? root.corrida.linea_base_s + " s antes, " + root.corrida.linea_base_s
                                                               + " s entre el entrenamiento y la inferencia, y " + root.corrida.linea_base_s + " s después"
                                                             : root.corrida.linea_base_s + " s antes y " + root.corrida.linea_base_s + " s después del entrenamiento")
                                     : "Sin línea base"
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "grupoSalidaEjecucion"; text: "DATOS DE SALIDA"
                        color: "#4ade80"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                    }
                    SummaryItem {
                        objectName: "resumenMonitoresSalida"
                        icon: "monitor"; title: "Monitores"
                        description: root.joinParts([
                            root.estadoMedidor("sistema") !== "" ? "Software: " + root.textoMedidor(root.estadoMedidor("sistema")) : "",
                            root.estadoMedidor("energia") !== "" ? "Energía: " + root.textoMedidor(root.estadoMedidor("energia")) : ""
                        ], " · ", "Sin monitores")
                        Layout.fillWidth: true
                    }
                    SummaryItem {
                        objectName: "resumenResultadoSalida"
                        icon: "barras"; title: "Resultado"
                        description: !root.terminada ? "Pendiente: la corrida todavía no ha terminado."
                                   : root.corrida.evaluacion && root.corrida.evaluacion.ejecutada
                                     ? "Evaluación en validación: " + root.textoMetricas(root.corrida.evaluacion.metricas || {})
                                     : "Sin métricas de evaluación disponibles; revisa Resultados."
                        Layout.fillWidth: true
                    }
                    SummaryItem { icon: "carpeta"; title: "Carpeta de la corrida"; description: root.corrida.dir_corrida || "Aún no creada"; Layout.fillWidth: true }

                    BotonIcono {
                        objectName: "botonCancelar"
                        visible: root.estado === "iniciando" || root.estado === "en_curso"
                        icono: "detener"
                        text: "Cancelar experimento"
                        Layout.fillWidth: true
                        implicitHeight: 36
                        font.pixelSize: Tipografia.destacado
                        font.bold: true
                        colorContenido: "#f87171"
                        background: Rectangle { radius: 8; color: "#3a1216"; border.color: "#f87171" }
                        onClicked: root.cliente.cancelar()
                    }
                    Text {
                        visible: root.estado === "cancelando"
                        text: "Cancelación cooperativa en curso: el modelo se detiene en su siguiente paso. Si no responde, puedes forzar la detención; la corrida quedará incompleta."
                        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    Button {
                        objectName: "botonForzar"
                        visible: root.estado === "cancelando"
                        text: "Forzar detención"
                        Layout.fillWidth: true
                        onClicked: confirmarForzar.open()
                    }
                    Button {
                        visible: root.terminada && !!root.corrida.dir_corrida
                        text: "Abrir carpeta de la corrida"
                        Layout.fillWidth: true
                        onClicked: Qt.openUrlExternally("file:///" + String(root.corrida.dir_corrida).replace(/\\/g, "/"))
                    }
                    Button {
                        objectName: "botonNuevo"
                        visible: root.terminada
                        text: "Nuevo experimento"
                        Layout.fillWidth: true
                        onClicked: { root.cliente.descartarResultado(); root.nuevoExperimentoSolicitado(); }
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                title: "Estado de monitores"

                MonitorStatusRow {
                    objectName: "estadoMonitorSoftware"
                    Layout.fillWidth: true
                    name: "Monitor de software"
                    detail: "Recursos computacionales: CPU, RAM"
                    status: root.textoMedidor(root.estadoMedidor("sistema"))
                    statusColor: root.colorMedidor(root.estadoMedidor("sistema"))
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                MonitorStatusRow {
                    objectName: "estadoMonitorEnergia"
                    Layout.fillWidth: true
                    name: "Monitor físico de energía"
                    detail: "Potencia, energía, voltaje, corriente"
                    status: root.textoMedidor(root.estadoMedidor("energia"))
                    statusColor: root.colorMedidor(root.estadoMedidor("energia"))
                }
            }
        }
    }

    DialogoNota {
        id: notaDecision
        textoAceptar: "Subir con esta nota"
        onNotaAceptada: (texto) => {
            if (root.historial) root.historial.guardarNota(root.corrida.dir_corrida, texto);
            root.subidas.decidir(root.corrida.dir_corrida, true);
        }
    }

    Dialog {
        id: confirmarForzar
        title: "Forzar la detención"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        Text {
            text: "Se terminará el proceso de ejecución. La corrida quedará incompleta y no se escribirán su evaluación ni su estado final."
            wrapMode: Text.WordWrap
            width: 360
        }
        onAccepted: root.cliente.forzarDetencion()
    }
}
