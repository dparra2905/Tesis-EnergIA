import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../components/Ayudas.js" as Ayudas
import "../components/Tipografia.js" as Tipografia

// Resultados de una corrida guardada, leídos de su carpeta (metadata.json, resultados.json y
// archivos de los medidores) mediante Historial (C++). Sin interpretaciones: solo datos.
// Con modoRemoto muestra una corrida de la base de datos (HistorialRemoto.detalle, D-011), con
// la misma forma calculada desde la serie unida.
Flickable {
    id: root
    contentWidth: width
    contentHeight: (hayCorrida ? mainRow.height : vacio.height) + 40
    clip: true

    property AppState appState
    // Regla de corrida activa (AppState): no se sube ni se exporta mientras una corrida mide
    readonly property bool corridaActiva: appState !== null && appState.corridaActiva
    // Historial (C++); null si la vista se abre sin backend
    property var historial: null
    // Carpeta de la corrida mostrada ("" = la más reciente)
    property string dirCorrida: ""
    // Cola de subidas a la base de datos (ColaSubidas, D-011); null sin backend
    property var subidas: null
    // Corrida de la base de datos (HistorialRemoto, D-011)
    property var remoto: null
    property bool modoRemoto: false
    // ClienteSupabase (C++): para editar la nota en la base; null sin backend
    property var sesion: null
    // Se incrementa al guardar la nota local para volver a leer el detalle
    property int revision: 0

    signal nuevoExperimentoSolicitado()
    // Modo remoto: volver a la página Base de datos
    signal volverSolicitado()

    readonly property var corridas: historial ? historial.corridas : []
    readonly property string dirEfectivo: dirCorrida !== "" ? dirCorrida : (corridas.length > 0 ? corridas[0].dir : "")
    readonly property var detalle: { var r = root.revision;
        return modoRemoto ? (remoto && remoto.estadoDetalle === "listo" ? remoto.detalle : ({}))
                          : (historial && dirEfectivo !== "" ? historial.detalle(dirEfectivo) : ({})); }
    readonly property bool hayCorrida: detalle !== undefined && detalle.dir !== undefined
    property string mensajeExportacion: ""
    // Exportar el modelo (D-013): solo corridas locales que guardaron un modelo exportable
    readonly property var infoModelo: { var r = root.revision;
        return !modoRemoto && historial && hayCorrida && historial.infoExportacion ? historial.infoExportacion(detalle.dir)
                                                                                  : ({ exportable: false, avisos: [] }); }
    readonly property bool exportandoModelo: historial !== null && historial.exportandoModelo === true
    function nombreZipModelo() {
        return (detalle.experimento_carpeta || detalle.experimento || "corrida") + "_corrida_"
               + ("00" + (detalle.corrida || 0)).slice(-3) + "_modelo.zip";
    }
    // Estado de la corrida en la base de datos (se reevalúa cuando cambia la cola)
    readonly property var estadoSubida: { var x = subidas ? subidas.elementos : []; return subidas && hayCorrida && !modoRemoto ? subidas.estadoDe(detalle.dir) : ({ estado: "" }); }
    readonly property string textoSubida: {
        if (estadoSubida.estado !== "subida" && estadoSubida.propietario === "otra")
            return "Se midió con otra cuenta en este equipo: solo su dueño puede subirla.";
        if (estadoSubida.estado !== "subida" && estadoSubida.propietario === "sin_cuenta")
            return "Se terminó sin cuenta. No se sube sola: súbela a tu cuenta si es tuya.";
        switch (estadoSubida.estado) {
        case "subida": return "Guardada en la base de datos" + (estadoSubida.id_remoto ? " (id " + estadoSubida.id_remoto + ")" : "")
                              + (estadoSubida.ultimo_error ? ". " + estadoSubida.ultimo_error : ".");
        case "subiendo": return "Subiendo a la base de datos…";
        case "pendiente": return "Pendiente de subir" + (estadoSubida.ultimo_error ? ": " + estadoSubida.ultimo_error : " (se sube al haber sesión y conexión).");
        case "esperando_decision": return "Esperando tu decisión: el modo de subida pide confirmar antes de subirla.";
        case "rechazada": return "La base de datos la rechazó: " + estadoSubida.ultimo_error;
        case "no_encolada": return "Todavía no está en la base de datos.";
        default: return "";
        }
    }

    // --- Auditoría (D-012) ---
    readonly property var auditoria: hayCorrida && detalle.auditoria && detalle.auditoria.comprobaciones ? detalle.auditoria : null
    function textoVeredicto(v) {
        return ({ valida: "Válida", con_advertencias: "Con advertencias", no_valida: "No válida" })[v] || "Sin auditoría";
    }
    function colorResultado(r) {
        return ({ ok: "#3fb950", advertencia: "#f59e0b", no_valida: "#f85149" })[r] || "#8b949e";
    }
    // Ancho de las columnas VALOR y RESULTADO de la tabla de auditoría
    readonly property int anchoColumnaAuditoria: 110
    function textoValor(c) {
        if (c.valor === null || c.valor === undefined) return "—";
        if (typeof c.valor === "number") return (Math.round(c.valor) === c.valor ? String(c.valor) : Number(c.valor).toFixed(2)) + (c.unidad ? " " + c.unidad : "");
        return String(c.valor);
    }

    // --- Nota de condiciones (D-012) ---
    // Se guarda en nota.txt de la carpeta local y, si la corrida también está en la base y es del
    // usuario con sesión, en la base (PATCH corrida.nota). Si el PATCH falla, la nota queda en
    // nota.txt y se ofrece reintentar.
    property string mensajeNota: ""
    property bool notaFallida: false
    property real idNotaBase: 0
    property string notaParaBase: ""
    function mismoInstante(a, b) { var x = Date.parse(a), y = Date.parse(b); return !isNaN(x) && x === y; }
    // Modo remoto: la carpeta local de la misma corrida en este equipo ("" si no está aquí)
    readonly property string dirLocalDeRemota: {
        if (!modoRemoto || !historial || !subidas || !hayCorrida || detalle.equipo !== subidas.equipo) return "";
        var cs = historial.corridas;
        for (var i = 0; i < cs.length; i++) if (cs[i].t_creacion && mismoInstante(cs[i].t_creacion, detalle.t_creacion)) return cs[i].dir;
        return "";
    }
    // Modo local: id de la corrida en la base si es del usuario con sesión (0 si no)
    function idRemotoPropio() {
        if (modoRemoto || !subidas || estadoSubida.estado !== "subida" || !estadoSubida.id_remoto) return 0;
        var lista = remoto ? remoto.corridas : [];
        for (var i = 0; i < lista.length; i++) if (lista[i].id === estadoSubida.id_remoto) return lista[i].propia ? estadoSubida.id_remoto : 0;
        return estadoSubida.origen === "cola" && estadoSubida.propietario === "propia" ? estadoSubida.id_remoto : 0;
    }
    // Solo el dueño edita la nota (D-012): en la vista local, una corrida de otra cuenta de este
    // equipo se ve pero no se edita
    readonly property bool notaEditable: hayCorrida && (!modoRemoto ? historial !== null && estadoSubida.propietario !== "otra"
                                                                    : detalle.propia === true)
    function enviarNotaBase(id, texto) {
        idNotaBase = id;
        notaParaBase = texto;
        if (!remoto || !sesion || !sesion.conectado) {
            notaFallida = true;
            mensajeNota = "La nota no se actualizó en la base: no hay conexión con la base de datos.";
            return;
        }
        remoto.cambiarNota(id, texto);
    }
    function guardarNota(texto) {
        mensajeNota = "";
        notaFallida = false;
        var dirLocal = modoRemoto ? dirLocalDeRemota : detalle.dir;
        if (dirLocal !== "" && historial) {
            var error = historial.guardarNota(dirLocal, texto);
            if (error !== "") { mensajeNota = error; return; }
            revision++;
        }
        var id = modoRemoto ? detalle.id_remoto : idRemotoPropio();
        if (id > 0) enviarNotaBase(id, texto);
        else mensajeNota = "Nota guardada en nota.txt (se sube con la corrida).";
    }
    Connections {
        target: root.remoto
        ignoreUnknownSignals: true
        function onNotaCambiada(id, nota, error) {
            if (id !== root.idNotaBase) return;
            root.notaFallida = error !== "";
            root.mensajeNota = error !== "" ? "La nota no se actualizó en la base: " + error
                                            : "Nota guardada" + (root.modoRemoto && root.dirLocalDeRemota === "" ? " en la base." : " en nota.txt y en la base.");
        }
    }
    DialogoNota {
        id: dialogoNota
        onNotaAceptada: (texto) => root.guardarNota(texto)
    }

    // --- Datos de las ventanas ---
    function ventana(nombre) { return hayCorrida && detalle.ventanas && detalle.ventanas[nombre] ? detalle.ventanas[nombre] : null; }
    function estad(nombre, grupo, serie) {
        var v = ventana(nombre);
        return v && v[grupo] && v[grupo][serie] && v[grupo][serie].n > 0 ? v[grupo][serie] : null;
    }
    function num(v, dec) { return v === null || v === undefined ? "—" : Number(v).toFixed(dec); }
    function energiaEntrenamiento() {
        return hayCorrida && detalle.energia_experimento && detalle.energia_experimento.entrenamiento
               ? detalle.energia_experimento.entrenamiento : null;
    }
    // Inferencia (D-014): metadata.inferencia y la ventana de energia 'inferencia' de resultados.json
    readonly property bool conInferencia: hayCorrida && detalle.inferencia !== undefined && detalle.inferencia.activada === true
    function ventanaEnergia(nombre) {
        return hayCorrida && detalle.energia_experimento && detalle.energia_experimento[nombre] ? detalle.energia_experimento[nombre] : null;
    }
    // Energia por prediccion (J): requiere el monitor de energia; null con el motivo si inferir no devolvio OK
    function textoEnergiaPrediccion() {
        var v = ventanaEnergia("inferencia"), inf = detalle.inferencia || {};
        if (inf.motivo_sin_por_prediccion) return "— (" + inf.motivo_sin_por_prediccion + ")";
        if (!midioEnergia) return "— (sin el monitor físico de energía)";
        if (!v || v.energia_por_prediccion_j === null || v.energia_por_prediccion_j === undefined) return "Sin calcular";
        return Number(v.energia_por_prediccion_j).toExponential(3) + " J";
    }
    // Tiempo por prediccion (µs): no requiere el Atorch (metadata.inferencia)
    function textoTiempoPrediccion() {
        var inf = detalle.inferencia || {};
        if (inf.motivo_sin_por_prediccion) return "— (" + inf.motivo_sin_por_prediccion + ")";
        if (inf.tiempo_por_prediccion_us === null || inf.tiempo_por_prediccion_us === undefined) return "—";
        return Number(inf.tiempo_por_prediccion_us).toPrecision(4) + " µs";
    }
    // Energia en bruto (D-008): todas las ventanas incluyen el consumo del equipo en reposo
    readonly property string notaEnBruto: "En bruto: incluye el consumo del equipo en reposo (la corrección por línea base no está decidida, D-008)"
    // Potencia media de la base previa (resultados.json), como referencia del reposo; sin restar nada
    function textoPotenciaBasePrevia() {
        var v = ventanaEnergia("base_pre");
        return v && v.potencia_media_w !== null && v.potencia_media_w !== undefined
               ? "base previa (reposo): " + num(v.potencia_media_w, 1) + " W" : "base previa (reposo): —";
    }
    function textoVentana(nombre) {
        var v = ventanaEnergia(nombre);
        return v ? num(v.energia_wh, 4) + " Wh" + (v.cobertura !== undefined ? " · cobertura " + num(100 * v.cobertura, 1) + " %" : "") : "—";
    }
    function fecha(iso) {
        if (!iso) return "—";
        var d = new Date(iso);
        if (isNaN(d.getTime())) return iso;
        function dd(n) { return n < 10 ? "0" + n : "" + n; }
        return dd(d.getDate()) + "/" + dd(d.getMonth() + 1) + "/" + d.getFullYear() + " " + dd(d.getHours()) + ":" + dd(d.getMinutes()) + ":" + dd(d.getSeconds());
    }
    function duracion(s) {
        if (s === null || s === undefined) return "—";
        var t = Math.round(s);
        function dd(n) { return n < 10 ? "0" + n : "" + n; }
        return dd(Math.floor(t / 3600)) + ":" + dd(Math.floor(t / 60) % 60) + ":" + dd(t % 60) + (s < 60 ? " (" + Number(s).toFixed(1) + " s)" : "");
    }
    function filasTabla(grupo, defs, nombreVentana) {
        var filas = [];
        for (var i = 0; i < defs.length; i++) {
            var e = estad(nombreVentana || "entrenamiento", grupo, defs[i].serie);
            filas.push({ label: defs[i].label, unit: defs[i].unit, color: defs[i].color,
                         minimo: e ? num(e.min, defs[i].dec) : "—", maximo: e ? num(e.max, defs[i].dec) : "—",
                         promedio: e ? num(e.media, defs[i].dec) : "—" });
        }
        return filas;
    }
    readonly property var defsRecursos: [
        { serie: "cpu_proceso_pct", label: "CPU · proceso de ejecución", unit: "% del equipo", color: "#3b82f6", dec: 1 },
        { serie: "cpu_sistema_pct", label: "CPU · sistema", unit: "%", color: "#60a5fa", dec: 1 },
        { serie: "ram_proceso_mb", label: "RAM · proceso de ejecución", unit: "MB", color: "#a78bfa", dec: 0 },
        { serie: "ram_sistema_pct", label: "RAM · sistema", unit: "%", color: "#c4b5fd", dec: 1 }
    ]
    readonly property var defsEnergia: [
        { serie: "potencia_w", label: "Potencia", unit: "W", color: "#f59e0b", dec: 1 },
        { serie: "voltaje_v", label: "Voltaje", unit: "V", color: "#22d3ee", dec: 1 },
        { serie: "corriente_a", label: "Corriente", unit: "A", color: "#f472b6", dec: 3 }
    ]
    readonly property var metricasModelo: {
        if (!hayCorrida) return [];
        var ev = detalle.evaluacion || {}, l = [];
        if (Object.keys(ev).length === 0 && detalle.col_accuracy_val !== undefined && detalle.col_accuracy_val !== null)
            ev = { val_accuracy: detalle.col_accuracy_val };
        for (var k in ev) {
            var v = Number(ev[k]);
            if (k === "val_accuracy")
                l.push({ clave: k, titulo: "Exactitud en validación", valor: num(100 * v, 2) + " %", detalle: k + " = " + v.toFixed(4) });
            else if (k === "val_loss")
                l.push({ clave: k, titulo: "Pérdida en validación", valor: v.toPrecision(4), detalle: k + " (sin unidad)" });
            else
                l.push({ clave: k, titulo: k, valor: v.toPrecision(4), detalle: "" });
        }
        return l;
    }
    readonly property bool midioSistema: hayCorrida && detalle.medidores.indexOf("sistema") >= 0
    readonly property bool midioEnergia: hayCorrida && detalle.medidores.indexOf("energia") >= 0

    FileDialog {
        id: dialogoExportar
        title: "Exportar la rejilla de 1 s de la corrida"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: {
            if (root.modoRemoto) {
                root.mensajeExportacion = "Descargando la serie unida…";
                root.remoto.exportarCsv(root.detalle.id_remoto, root.remoto.rutaLocal(selectedFile));
                return;
            }
            var error = root.historial.exportarCsv(root.dirEfectivo, root.historial.rutaLocal(selectedFile));
            root.mensajeExportacion = error === "" ? "Exportado: " + root.historial.rutaLocal(selectedFile) : error;
        }
    }

    FileDialog {
        id: dialogoExportarModelo
        title: "Exportar el modelo de la corrida (.zip)"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: ["Archivo ZIP (*.zip)"]
        onAccepted: {
            root.mensajeExportacion = "Exportando el modelo…";
            root.historial.exportarModelo(root.detalle.dir, root.historial.rutaLocal(selectedFile));
        }
    }

    Connections {
        target: root.modoRemoto ? null : root.historial
        ignoreUnknownSignals: true
        function onModeloExportado(dir, destino, error, avisos) {
            root.mensajeExportacion = error !== "" ? "No se pudo exportar el modelo: " + error
                                                   : "Modelo exportado: " + destino
                                                     + (avisos.length > 0 ? " (con " + avisos.length + " aviso(s): ver LEEME.txt)" : "");
        }
    }

    Connections {
        target: root.modoRemoto ? root.remoto : null
        ignoreUnknownSignals: true
        function onExportacionTerminada(id, destino, filas, error) {
            root.mensajeExportacion = error !== "" ? error : "Exportado (" + filas + " filas): " + destino;
        }
        function onCorridaBorrada(id, error) {
            if (error !== "") root.mensajeExportacion = error;
            else root.volverSolicitado();
        }
    }

    Dialog {
        id: confirmarBorrar
        objectName: "confirmarBorrar"
        title: "Borrar la corrida de la base de datos"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        Text {
            text: "Se borran de la base la corrida y su serie unida. La carpeta local del equipo donde se midió no se toca."
            color: "#c9d1d9"; font.pixelSize: Tipografia.destacado; wrapMode: Text.WordWrap; width: 360
        }
        onAccepted: root.remoto.borrar(root.detalle.id_remoto)
    }

    // Sin corridas
    ColumnLayout {
        id: vacio
        visible: !root.hayCorrida
        x: 24; y: 20
        width: parent.width - 48
        SectionCard {
            objectName: "tarjetaVacia"
            Layout.fillWidth: true
            title: !root.modoRemoto ? "Todavía no hay resultados"
                 : root.remoto && root.remoto.estadoDetalle === "error" ? "No se pudo cargar la corrida"
                 : "Cargando la corrida desde la base de datos…"
            subtitle: root.modoRemoto ? (root.remoto && root.remoto.estadoDetalle === "error" ? root.remoto.errorDetalle : "")
                    : root.historial ? "No hay corridas guardadas en " + root.historial.dirSalida + "."
                                     : "Vista sin backend: no hay corridas que mostrar."
            BotonIcono {
                icono: root.modoRemoto ? "atras" : "mas"
                text: root.modoRemoto ? "Volver a la base de datos" : "Configurar un experimento"
                onClicked: root.modoRemoto ? root.volverSolicitado() : root.nuevoExperimentoSolicitado()
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

            // Corrida de la base de datos: de quién es y dónde se midió
            RowLayout {
                objectName: "cabeceraRemota"
                visible: root.modoRemoto
                Layout.fillWidth: true
                spacing: 10
                Enlace { text: "Base de datos"; icono: "atras"; onClicked: root.volverSolicitado() }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.hayCorrida && root.modoRemoto
                          ? root.detalle.experimento + " / corrida " + root.detalle.corrida + " · " + root.detalle.equipo
                            + " · " + (root.detalle.propia ? "tuya" : "de " + (root.detalle.dueno || "otro usuario"))
                            + " · " + root.detalle.visibilidad
                          : ""
                    color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true
                }
            }

            // Selector de corrida
            RowLayout {
                visible: !root.modoRemoto
                Layout.fillWidth: true
                spacing: 10
                Text { text: "Corrida"; color: "#8b949e"; font.pixelSize: Tipografia.cuerpo }
                ComboBox {
                    id: selector
                    objectName: "selectorCorrida"
                    Layout.fillWidth: true
                    model: root.corridas.map(function (c) { return c.id + " · " + c.modelo_nombre + " · " + c.estado; })
                    currentIndex: {
                        for (var i = 0; i < root.corridas.length; i++)
                            if (root.corridas[i].dir === root.dirEfectivo) return i;
                        return -1;
                    }
                    onActivated: (i) => root.dirCorrida = root.corridas[i].dir
                }
                EstadoBase {
                    objectName: "estadoBaseResultados"
                    estado: root.subidas && root.hayCorrida ? root.estadoSubida : null
                }
            }

            // Información general
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: infoRow.height + 32
                radius: 12; color: "#161b22"; border.color: "#21262d"

                RowLayout {
                    id: infoRow
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 20

                    InfoBlock {
                        label: "Tipo de modelo"
                        value: root.hayCorrida ? root.detalle.modelo_nombre : ""
                        sub: root.hayCorrida ? (root.detalle.modelo_interno ? "Prueba interna · no es un modelo del estudio" : root.detalle.modelo_clave) : ""
                    }
                    InfoBlock {
                        label: "Datos"
                        value: root.hayCorrida ? (root.detalle.datos_origen === "csv" ? String(root.detalle.datos_ruta).split(/[\\/]/).pop() : "Sintéticos (prueba)") : ""
                        sub: root.hayCorrida && root.detalle.n_train !== undefined ? root.detalle.n_train + " entrenamiento · " + root.detalle.n_val + " validación" : ""
                    }
                    InfoBlock {
                        objectName: "estadoResultado"
                        label: "Estado"
                        value: root.hayCorrida ? root.detalle.estado : ""
                        sub: root.hayCorrida ? root.detalle.error : ""
                        accent: root.hayCorrida && root.detalle.estado === "completada" ? "#22c55e"
                                : (root.hayCorrida && root.detalle.estado === "cancelada" ? "#f59e0b" : "#f87171")
                    }
                    InfoBlock {
                        objectName: "veredictoResultado"
                        label: "Auditoría"
                        value: root.hayCorrida ? root.textoVeredicto(root.detalle.veredicto) : ""
                        sub: root.auditoria ? "reglas v" + root.auditoria.version_reglas + " · umbrales provisionales" : ""
                        accent: root.hayCorrida ? ({ valida: "#3fb950", con_advertencias: "#f59e0b", no_valida: "#f85149" })[root.detalle.veredicto] || "#8b949e" : "#8b949e"
                    }
                    InfoBlock { label: "Duración del entrenamiento"; value: root.hayCorrida ? root.duracion(root.detalle.duracion_entrenamiento_s) : "" }
                    InfoBlock { label: "Inicio (t_start)"; value: root.hayCorrida ? root.fecha(root.detalle.t_start) : "" }
                }
            }

            // Desempeño del modelo: evaluación de la DLL y métricas de clasificación (D-015) en una sola
            // sección, separada del consumo
            CalidadModelo {
                Layout.fillWidth: true
                metricas: root.hayCorrida && root.detalle.metricas_modelo ? root.detalle.metricas_modelo : ({})
                evaluacion: root.metricasModelo
            }

            // Energía (monitor físico)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                MetricGroupLabel {
                    objectName: "grupoEnergia"
                    text: "Métricas de energía · Monitor físico de energía (medidor Atorch S1BW) · durante el entrenamiento"
                    accent: root.appState.energyAccent
                    ayuda: Ayudas.texto("grupo_energia")
                }
                Text {
                    visible: !root.midioEnergia
                    text: "Esta corrida no incluyó el monitor físico de energía."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                }
                GridLayout {
                    visible: root.midioEnergia
                    Layout.fillWidth: true
                    columns: root.width >= 1520 ? 4 : 2
                    columnSpacing: 12
                    rowSpacing: 12
                    StatCard {
                        objectName: "statEnergia"
                        title: "Energía del experimento"; dotColor: "#22c55e"; Layout.fillWidth: true
                        ayuda: Ayudas.texto("energia")
                        value: root.energiaEntrenamiento() ? root.num(root.energiaEntrenamiento().energia_wh, 4) + " Wh" : "Sin calcular"
                        caption: !root.energiaEntrenamiento() ? "falta resultados.json"
                                 : root.energiaEntrenamiento().cobertura === undefined ? "en bruto · dato derivado (D-008)"
                                 : "cobertura " + root.num(100 * root.energiaEntrenamiento().cobertura, 1) + " % · en bruto · dato derivado (D-008)"
                    }
                    StatCard {
                        objectName: "statPotenciaEntrenamiento"
                        title: "Potencia (promedio)"; dotColor: "#f59e0b"; Layout.fillWidth: true
                        ayuda: Ayudas.texto("potencia_w")
                        value: root.num(root.estad("entrenamiento", "energia", "potencia_w") ? root.estad("entrenamiento", "energia", "potencia_w").media : null, 1) + " W"
                        caption: root.textoPotenciaBasePrevia()
                    }
                    StatCard {
                        title: "Voltaje (promedio)"; dotColor: "#22d3ee"; Layout.fillWidth: true
                        ayuda: Ayudas.texto("voltaje_v")
                        value: root.num(root.estad("entrenamiento", "energia", "voltaje_v") ? root.estad("entrenamiento", "energia", "voltaje_v").media : null, 1) + " V"
                    }
                    StatCard {
                        title: "Corriente (promedio)"; dotColor: "#f472b6"; Layout.fillWidth: true
                        ayuda: Ayudas.texto("corriente_a")
                        value: root.num(root.estad("entrenamiento", "energia", "corriente_a") ? root.estad("entrenamiento", "energia", "corriente_a").media : null, 3) + " A"
                    }
                }
                Text {
                    objectName: "notaEnBrutoEntrenamiento"
                    visible: root.midioEnergia
                    text: root.notaEnBruto + "."
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

            // Recursos (monitor de software)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                MetricGroupLabel {
                    objectName: "grupoRecursos"
                    text: "Uso de recursos computacionales (CPU y RAM) · Monitor de software del sistema operativo · durante el entrenamiento"
                    accent: root.appState.resourcesAccent
                    ayuda: Ayudas.texto("grupo_recursos")
                }
                Text {
                    visible: !root.midioSistema
                    text: "Esta corrida no incluyó el monitor de software."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                }
                GridLayout {
                    visible: root.midioSistema
                    Layout.fillWidth: true
                    columns: root.width >= 1520 ? 4 : 2
                    columnSpacing: 12
                    rowSpacing: 12
                    StatCard {
                        objectName: "statCpuProceso"
                        title: "CPU del proceso (promedio)"; dotColor: "#3b82f6"; Layout.fillWidth: true
                        value: root.num(root.estad("entrenamiento", "sistema", "cpu_proceso_pct") ? root.estad("entrenamiento", "sistema", "cpu_proceso_pct").media : null, 1) + " %"
                        caption: "del total del equipo"
                        ayuda: Ayudas.texto("cpu_proceso_pct")
                    }
                    StatCard {
                        title: "CPU del sistema (promedio)"; dotColor: "#60a5fa"; Layout.fillWidth: true
                        value: root.num(root.estad("entrenamiento", "sistema", "cpu_sistema_pct") ? root.estad("entrenamiento", "sistema", "cpu_sistema_pct").media : null, 1) + " %"
                        ayuda: Ayudas.texto("cpu_sistema_pct")
                    }
                    StatCard {
                        title: "RAM del proceso (máximo)"; dotColor: "#a78bfa"; Layout.fillWidth: true
                        value: root.num(root.estad("entrenamiento", "sistema", "ram_proceso_mb") ? root.estad("entrenamiento", "sistema", "ram_proceso_mb").max : null, 0) + " MB"
                        ayuda: Ayudas.texto("ram_proceso_mb")
                    }
                    StatCard {
                        title: "RAM del sistema (promedio)"; dotColor: "#c4b5fd"; Layout.fillWidth: true
                        value: root.num(root.estad("entrenamiento", "sistema", "ram_sistema_pct") ? root.estad("entrenamiento", "sistema", "ram_sistema_pct").media : null, 1) + " %"
                        ayuda: Ayudas.texto("ram_sistema_pct")
                    }
                }
            }

            // Gráficas sobre la rejilla de 1 s (los huecos no se interpolan)
            SectionCard {
                visible: root.midioEnergia || root.midioSistema
                Layout.fillWidth: true
                title: "Evolución durante la corrida"
                subtitle: "Rejilla de 1 s desde t_start; entrenamiento entre " + root.num(root.detalle.t_antes_rel_s, 1) + " s y "
                          + root.num(root.detalle.t_despues_rel_s, 1) + " s. Los huecos sin datos no se interpolan."

                Text { visible: root.midioEnergia; text: "Potencia (W) · monitor físico de energía"; color: "#f59e0b"; font.pixelSize: Tipografia.cuerpo }
                LineChart {
                    visible: root.midioEnergia
                    Layout.fillWidth: true
                    implicitHeight: 160
                    unidad: "W"
                    series: root.hayCorrida && root.detalle.series_energia && root.detalle.series_energia.potencia_w
                            ? [{ name: "Potencia", color: "#f59e0b", values: root.detalle.series_energia.potencia_w }] : []
                }
                Text { visible: root.midioSistema; text: "CPU del proceso de ejecución (% del equipo) · monitor de software"; color: "#3b82f6"; font.pixelSize: Tipografia.cuerpo }
                LineChart {
                    visible: root.midioSistema
                    Layout.fillWidth: true
                    implicitHeight: 160
                    unidad: "%"
                    series: root.hayCorrida && root.detalle.series_sistema && root.detalle.series_sistema.cpu_proceso_pct
                            ? [{ name: "CPU proceso", color: "#3b82f6", values: root.detalle.series_sistema.cpu_proceso_pct }] : []
                }
            }

            // RAM en la misma línea temporal de las mediciones ya almacenadas.
            SectionCard {
                objectName: "tarjetaRamResultados"
                visible: root.midioSistema
                Layout.fillWidth: true
                title: "Evolución del consumo de RAM"
                subtitle: "Lecturas del monitor de software durante la corrida · sin interpolar muestras ausentes"
                accent: "#a78bfa"

                Text {
                    text: "RAM del proceso de ejecución (MB)"
                    color: "#a78bfa"; font.pixelSize: Tipografia.cuerpo; font.bold: true
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                LineChart {
                    objectName: "graficaRamProcesoResultados"
                    Layout.fillWidth: true
                    implicitHeight: 172
                    unidad: "MB"
                    series: root.hayCorrida && root.detalle.series_sistema && root.detalle.series_sistema.ram_proceso_mb
                            ? [{ name: "RAM proceso", color: "#a78bfa", values: root.detalle.series_sistema.ram_proceso_mb }] : []
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                Text {
                    text: "RAM ocupada del sistema completo (%)"
                    color: "#c4b5fd"; font.pixelSize: Tipografia.cuerpo; font.bold: true
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
                LineChart {
                    objectName: "graficaRamSistemaResultados"
                    Layout.fillWidth: true
                    implicitHeight: 172
                    unidad: "%"
                    series: root.hayCorrida && root.detalle.series_sistema && root.detalle.series_sistema.ram_sistema_pct
                            ? [{ name: "RAM sistema", color: "#c4b5fd", values: root.detalle.series_sistema.ram_sistema_pct }] : []
                }
            }

            // Detalle por grupo
            SectionCard {
                Layout.fillWidth: true
                title: "Detalle de métricas durante el entrenamiento"
                subtitle: "Mínimo, máximo y promedio en [t_antes, t_despues]"

                MetricTable {
                    visible: root.midioEnergia
                    Layout.fillWidth: true
                    title: "Métricas de energía · monitor físico de energía"
                    accent: root.appState.energyAccent
                    rows: root.hayCorrida ? root.filasTabla("energia", root.defsEnergia) : []
                }
                MetricTable {
                    visible: root.midioSistema
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    title: "Uso de recursos computacionales · monitor de software"
                    accent: root.appState.resourcesAccent
                    rows: root.hayCorrida ? root.filasTabla("sistema", root.defsRecursos) : []
                }
                Text {
                    visible: !root.midioSistema && !root.midioEnergia
                    text: "Sin medidores en esta corrida: solo se registraron el progreso y los tiempos."
                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                }
            }

            // Inferencia (D-014), separada del entrenamiento
            SectionCard {
                objectName: "tarjetaInferencia"
                visible: root.conInferencia
                Layout.fillWidth: true
                title: "Inferencia"
                subtitle: root.conInferencia
                          ? "Predicciones sobre la partición de validación ("
                            + (root.detalle.inferencia.n_filas !== undefined && root.detalle.inferencia.n_filas !== null ? root.detalle.inferencia.n_filas + " filas, " : "")
                            + "lote de " + root.detalle.inferencia.lote + ") durante al menos " + root.detalle.inferencia.duracion_min_s
                            + " s, entre " + root.num(root.detalle.t_inf_antes_rel_s, 1) + " s y " + root.num(root.detalle.t_inf_despues_rel_s, 1) + " s"
                          : ""

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 1500 ? 5 : root.width >= 1000 ? 3 : 2
                    columnSpacing: 12
                    rowSpacing: 12
                    StatCard {
                        objectName: "statEnergiaInferencia"
                        title: "Energía de la inferencia"; dotColor: "#22c55e"; Layout.fillWidth: true
                        value: !root.midioEnergia ? "Sin monitor de energía"
                               : root.ventanaEnergia("inferencia") ? root.num(root.ventanaEnergia("inferencia").energia_wh, 4) + " Wh" : "Sin calcular"
                        caption: root.ventanaEnergia("inferencia") && root.ventanaEnergia("inferencia").cobertura !== undefined
                                 ? "cobertura " + root.num(100 * root.ventanaEnergia("inferencia").cobertura, 1) + " % · en bruto · dato derivado (D-008)" : ""
                    }
                    StatCard {
                        objectName: "statEnergiaPrediccion"
                        title: "Energía por predicción"; dotColor: "#a3e635"; Layout.fillWidth: true
                        value: root.conInferencia ? root.textoEnergiaPrediccion() : ""
                        caption: root.midioEnergia ? "en bruto (D-008)" : ""
                    }
                    StatCard {
                        objectName: "statPotenciaInferencia"
                        visible: root.midioEnergia
                        title: "Potencia (promedio)"; dotColor: "#f59e0b"; Layout.fillWidth: true
                        value: root.num(root.estad("inferencia", "energia", "potencia_w") ? root.estad("inferencia", "energia", "potencia_w").media : null, 1) + " W"
                        caption: root.textoPotenciaBasePrevia()
                    }
                    StatCard {
                        objectName: "statTiempoPrediccion"
                        title: "Tiempo por predicción"; dotColor: "#38bdf8"; Layout.fillWidth: true
                        value: root.conInferencia ? root.textoTiempoPrediccion() : ""
                    }
                    StatCard {
                        objectName: "statPredicciones"
                        title: "Predicciones"; dotColor: "#c084fc"; Layout.fillWidth: true
                        value: root.conInferencia && root.detalle.inferencia.n_predicciones !== null && root.detalle.inferencia.n_predicciones !== undefined
                               ? String(root.detalle.inferencia.n_predicciones) : "—"
                        caption: root.conInferencia && root.detalle.inferencia.pasadas !== null && root.detalle.inferencia.pasadas !== undefined
                                 ? root.detalle.inferencia.pasadas + " pasadas en " + root.detalle.inferencia.grupos_registrados + " grupos · "
                                   + root.num(root.detalle.inferencia.duracion_ms / 1000, 1) + " s"
                                 : ""
                    }
                }
                Text {
                    objectName: "ventanasEnergia"
                    visible: root.midioEnergia
                    text: "Ventanas de energía (en bruto, sin corregir por la línea base, D-008): base previa " + root.textoVentana("base_pre")
                          + "; entrenamiento " + root.textoVentana("entrenamiento") + "; base intermedia " + root.textoVentana("base_media")
                          + "; inferencia " + root.textoVentana("inferencia") + "; base posterior " + root.textoVentana("base_post") + "."
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    objectName: "notaEnBrutoInferencia"
                    visible: root.midioEnergia
                    text: "ⓘ " + root.notaEnBruto + ". Vale para la energía de la inferencia y la energía por predicción."
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                MetricTable {
                    visible: root.midioSistema
                    Layout.fillWidth: true
                    title: "Uso de recursos computacionales durante la inferencia"
                    accent: root.appState.resourcesAccent
                    rows: root.conInferencia ? root.filasTabla("sistema", root.defsRecursos, "inferencia") : []
                }
                MetricTable {
                    visible: root.midioEnergia
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    title: "Métricas de energía durante la inferencia"
                    accent: root.appState.energyAccent
                    rows: root.conInferencia ? root.filasTabla("energia", root.defsEnergia, "inferencia") : []
                }
            }

            // Auditoría de la corrida (D-012): cada comprobación con su valor, umbral, resultado y motivo
            SectionCard {
                objectName: "tarjetaAuditoria"
                Layout.fillWidth: true
                title: "Auditoría de la corrida"
                subtitle: root.auditoria ? "Informa; no bloquea la corrida ni la subida. Reglas v" + root.auditoria.version_reglas
                                           + ", umbrales provisionales (se ajustarán con más corridas)."
                                         : "Esta corrida no tiene auditoría: es anterior a la auditoría o no se pudo calcular. Se calcula con "
                                           + "energia_orquestador --postprocesar o al subirla."

                RowLayout {
                    visible: root.auditoria !== null
                    Layout.fillWidth: true
                    Text { text: "COMPROBACIÓN"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 170 }
                    Text { text: "VALOR"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: root.anchoColumnaAuditoria }
                    Text { text: "RESULTADO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: root.anchoColumnaAuditoria }
                    Text { text: "UMBRAL Y MOTIVO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true }
                }
                Repeater {
                    objectName: "comprobacionesAuditoria"
                    model: root.auditoria ? root.auditoria.comprobaciones : []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        ColumnLayout {
                            Layout.preferredWidth: 170
                            Layout.alignment: Qt.AlignTop
                            spacing: 1
                            Text { text: modelData.id; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            Text { text: modelData.descripcion; color: "#8b949e"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.preferredWidth: 170 }
                        }
                        // Valor y resultado se ajustan en varias líneas dentro de su columna
                        // ("0 medidores con problemas", "no_evaluable") en vez de montarse sobre la siguiente
                        Text {
                            objectName: "valorAuditoria_" + modelData.id
                            text: root.textoValor(modelData); color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                            wrapMode: Text.Wrap
                            Layout.preferredWidth: root.anchoColumnaAuditoria; Layout.maximumWidth: root.anchoColumnaAuditoria
                            Layout.alignment: Qt.AlignTop
                        }
                        Text {
                            objectName: "resultadoAuditoria_" + modelData.id
                            text: modelData.resultado; color: root.colorResultado(modelData.resultado)
                            font.pixelSize: Tipografia.cuerpo; wrapMode: Text.Wrap
                            Layout.preferredWidth: root.anchoColumnaAuditoria; Layout.maximumWidth: root.anchoColumnaAuditoria
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 1
                            Text { text: modelData.umbral; color: "#8b949e"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: modelData.motivo; color: "#c9d1d9"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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

            SectionCard {
                Layout.fillWidth: true
                title: "Datos de la corrida"

                Repeater {
                    model: {
                        if (!root.hayCorrida) return [];
                        var d = root.detalle, l = [];
                        l.push({ t: "Pasos registrados", v: d.pasos_registrados + " " + d.unidad_paso });
                        l.push({ t: "Inferencia", v: root.conInferencia
                                     ? "Activada · " + (d.inferencia.status_texto || "no empezó") + " · lote " + d.inferencia.lote
                                     : "Sin inferencia" });
                        if (d.hilos_efectivos !== undefined && d.hilos_efectivos !== null)
                            l.push({ t: "Hilos usados", v: String(d.hilos_efectivos) });
                        var hp = [];
                        for (var h in d.hiperparametros) hp.push(h + " = " + d.hiperparametros[h]);
                        l.push({ t: "Hiperparámetros", v: hp.join(", ") });
                        l.push({ t: "Monitores", v: d.medidores.length ? d.medidores.join(", ") : "Ninguno" });
                        if (d.metodo_energia)
                            l.push({ t: "Cálculo de energía", v: d.metodo_energia + " v" + d.version_metodo_energia
                                       + " (sin corregir por la línea base; validación con el Atorch real pendiente, D-008)" });
                        if (root.modoRemoto) {
                            var hw = d.hardware;
                            l.push({ t: "Equipo", v: d.equipo + (hw ? " · " + (hw.cpu || "CPU sin datos") + " · " + hw.nucleos_logicos + " núcleos · "
                                                                   + root.num(hw.ram_total_bytes / 1073741824, 1) + " GB RAM · " + (hw.sistema_operativo || "")
                                                                 : " (sin datos del hardware)") });
                            l.push({ t: "Dueño", v: d.propia ? "Tú" : (d.dueno || "Otro usuario") });
                            if (d.dataset) l.push({ t: "Dataset", v: d.dataset.nombre + " · sha256 " + String(d.dataset.sha256).substring(0, 12) + "…" });
                            l.push({ t: "Subida", v: root.fecha(d.subida_en) });
                        } else {
                            l.push({ t: "Carpeta", v: d.dir });
                        }
                        return l;
                    }
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: modelData.t; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar }
                        Text { text: modelData.v; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    }
                }
            }

            // Nota de condiciones (D-012)
            SectionCard {
                objectName: "tarjetaNota"
                Layout.fillWidth: true
                title: "Nota de condiciones"
                Text {
                    objectName: "textoNota"
                    text: root.hayCorrida && root.detalle.nota ? root.detalle.nota : "Sin nota."
                    color: root.hayCorrida && root.detalle.nota ? "#c9d1d9" : "#8b949e"
                    font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Button {
                    objectName: "botonEditarNota"
                    visible: root.notaEditable
                    text: root.hayCorrida && root.detalle.nota ? "Editar la nota" : "Escribir una nota"
                    Layout.fillWidth: true
                    onClicked: { dialogoNota.texto = root.detalle.nota || ""; dialogoNota.open(); }
                }
                Text {
                    visible: root.hayCorrida && !root.notaEditable
                    text: "Solo el dueño de la corrida puede editar su nota."
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    objectName: "mensajeNota"
                    visible: root.mensajeNota !== ""
                    text: root.mensajeNota
                    color: root.notaFallida ? "#f59e0b" : "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Button {
                    objectName: "botonReintentarNota"
                    visible: root.notaFallida
                    text: "Reintentar en la base"
                    Layout.fillWidth: true
                    onClicked: { root.mensajeNota = ""; root.notaFallida = false; root.enviarNotaBase(root.idNotaBase, root.notaParaBase); }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                title: "Acciones"

                // Copia en la base de datos (D-011): la corrida local es la fuente de verdad
                Text {
                    objectName: "estadoSubida"
                    visible: root.subidas !== null && !root.modoRemoto
                    text: root.textoSubida
                    color: root.estadoSubida.estado === "subida" ? "#3fb950"
                         : root.estadoSubida.estado === "rechazada" ? "#f85149" : "#8b949e"
                    font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                // Al subir a mano no se pregunta, pero se ve lo que se sube: veredicto y nota (D-012)
                RowLayout {
                    visible: root.subidas !== null && !root.modoRemoto && root.estadoSubida.estado !== "subida"
                    Layout.fillWidth: true
                    Veredicto { veredicto: root.hayCorrida ? (root.detalle.veredicto || "") : "" }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.hayCorrida && root.detalle.nota ? "Con nota" : "Sin nota"
                        color: "#3b82f6"; font.pixelSize: Tipografia.auxiliar
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { dialogoNota.texto = root.detalle.nota || ""; dialogoNota.open(); } }
                    }
                }
                BotonIcono {
                    objectName: "botonSubir"
                    visible: root.subidas !== null && !root.modoRemoto && root.estadoSubida.propietario !== "otra"
                             && (root.estadoSubida.estado === "no_encolada" || root.estadoSubida.estado === "rechazada"
                                 || root.estadoSubida.estado === "pendiente" || root.estadoSubida.estado === "esperando_decision")
                    icono: root.estadoSubida.propietario === "sin_cuenta" || root.estadoSubida.estado === "no_encolada" || root.estadoSubida.estado === "esperando_decision" ? "subir" : "recargar"
                    text: root.estadoSubida.propietario === "sin_cuenta" ? "Subir a mi cuenta"
                        : root.estadoSubida.estado === "no_encolada" || root.estadoSubida.estado === "esperando_decision" ? "Subir a la base de datos" : "Reintentar la subida"
                    enabled: !root.corridaActiva
                    Layout.fillWidth: true
                    onClicked: root.subidas.reintentar(root.detalle.dir)
                }

                BotonIcono {
                    objectName: "botonExportar"
                    icono: "descargar"
                    text: "Exportar CSV (rejilla de 1 s)"
                    enabled: (root.midioSistema || root.midioEnergia) && !root.corridaActiva
                    Layout.fillWidth: true
                    onClicked: dialogoExportar.open()
                }
                // Exportar el modelo (D-013): .zip con el modelo, modelo.json y LEEME.txt; no se sube a la base
                Button {
                    objectName: "botonExportarModelo"
                    visible: !root.modoRemoto && root.infoModelo.exportable === true
                    text: root.exportandoModelo ? "Exportando el modelo…" : "Exportar modelo"
                    enabled: !root.corridaActiva && !root.exportandoModelo
                    Layout.fillWidth: true
                    onClicked: {
                        // Nombre propuesto, en la carpeta de salida de las corridas
                        dialogoExportarModelo.selectedFile = "file:///" + String(root.historial.dirSalida).replace(/\\/g, "/")
                                                             + "/" + root.nombreZipModelo();
                        dialogoExportarModelo.open();
                    }
                }
                Text {
                    objectName: "avisosModelo"
                    visible: !root.modoRemoto && root.infoModelo.exportable === true && (root.infoModelo.avisos || []).length > 0
                    text: (root.infoModelo.avisos || []).join("\n")
                    color: "#f59e0b"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    objectName: "avisoCorridaActiva"
                    visible: root.corridaActiva
                    text: root.appState ? root.appState.avisoCorridaActiva : ""
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Text {
                    visible: root.mensajeExportacion !== ""
                    text: root.mensajeExportacion
                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
                }
                BotonIcono {
                    objectName: "botonBorrarRemota"
                    visible: root.modoRemoto && root.hayCorrida && root.detalle.propia === true
                    icono: "papelera"
                    text: "Borrar de la base de datos"
                    Layout.fillWidth: true
                    onClicked: confirmarBorrar.open()
                }
                BotonIcono {
                    visible: !root.modoRemoto
                    icono: "carpeta"
                    text: "Abrir carpeta de la corrida"
                    Layout.fillWidth: true
                    onClicked: Qt.openUrlExternally("file:///" + String(root.detalle.dir).replace(/\\/g, "/"))
                }
                BotonIcono {
                    icono: "mas"
                    text: "Nuevo experimento"
                    Layout.fillWidth: true
                    onClicked: root.nuevoExperimentoSolicitado()
                }
            }
        }
    }

    component MetricGroupLabel: RowLayout {
        id: groupLabel
        property string text: ""
        property color accent: "#8b949e"
        property string ayuda: ""
        Layout.fillWidth: true
        spacing: 8
        Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 12; radius: 2; color: groupLabel.accent }
        Text { text: groupLabel.text; color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Ayuda { visible: groupLabel.ayuda !== ""; texto: groupLabel.ayuda }
    }

    component MetricTable: ColumnLayout {
        id: table
        property string title: ""
        property color accent: "#8b949e"
        property var rows: []   // [{ label, unit, color, minimo, maximo, promedio }]
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 12; radius: 2; color: table.accent }
            Text {
                text: table.title; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "MÉTRICA"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 180 }
            Text { text: "MÍNIMO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight }
            Text { text: "MÁXIMO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight }
            Text { text: "PROMEDIO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight }
            Text { text: "UNIDAD"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 80 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }

        Repeater {
            model: table.rows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                RowLayout {
                    Layout.preferredWidth: 180
                    Layout.maximumWidth: 180
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 6; Layout.preferredHeight: 6
                        Layout.alignment: Qt.AlignVCenter
                        radius: 3; color: modelData.color
                    }
                    Text {
                        text: modelData.label; color: "#e6edf3"
                        font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                Text { text: modelData.minimo; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight; wrapMode: Text.NoWrap }
                Text { text: modelData.maximo; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight; wrapMode: Text.NoWrap }
                Text { text: modelData.promedio; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight; wrapMode: Text.NoWrap }
                Text { text: modelData.unit; color: "#484f58"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 80 }
            }
        }
    }

    component InfoBlock: ColumnLayout {
        id: bloque
        property string label: ""
        property string value: ""
        property string sub: ""
        property string ayuda: ""
        property color accent: "#e6edf3"
        Layout.fillWidth: true
        spacing: 2
        Row {
            spacing: 4
            Text { text: bloque.label; color: "#484f58"; font.pixelSize: Tipografia.detalle; anchors.verticalCenter: parent.verticalCenter }
            Ayuda { visible: bloque.ayuda !== ""; texto: bloque.ayuda; tamano: 11; anchors.verticalCenter: parent.verticalCenter }
        }
        Text { text: value; color: accent; font.pixelSize: Tipografia.destacado; font.bold: true }
        Text { text: sub; color: "#8b949e"; font.pixelSize: Tipografia.detalle; visible: sub !== ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
}
