import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/Ayudas.js" as Ayudas
import "../components/Tipografia.js" as Tipografia

// Comparación de corridas de la base de datos (D-011): de 2 a 4 corridas lado a lado, con su
// equipo (el consumo depende del hardware), modelo, datos y consumo durante el entrenamiento.
// Sin interpretaciones: solo datos, como Resultados.
Flickable {
    id: root
    contentWidth: width
    contentHeight: columna.height + 40
    clip: true

    property AppState appState
    // HistorialRemoto (C++): comparacion, estadoComparacion, errorComparacion
    property var remoto: null

    signal volverSolicitado()
    signal verCorrida(real id)

    readonly property var corridas: remoto && remoto.estadoComparacion === "listo" ? remoto.comparacion : []
    readonly property var colores: ["#3b82f6", "#f59e0b", "#22c55e", "#f472b6"]

    function num(v, dec) { return v === null || v === undefined || isNaN(v) ? "—" : Number(v).toFixed(dec); }
    function duracion(s) {
        if (s === null || s === undefined) return "—";
        var t = Math.round(s);
        function dd(n) { return n < 10 ? "0" + n : "" + n; }
        return dd(Math.floor(t / 3600)) + ":" + dd(Math.floor(t / 60) % 60) + ":" + dd(t % 60);
    }
    function energiaEnt(d) {
        var e = d.energia_experimento && d.energia_experimento.entrenamiento ? d.energia_experimento.entrenamiento.energia_wh : undefined;
        return e !== undefined ? e : d.col_energia_entrenamiento_wh;
    }
    function energiaMed(d) {
        var e = d.energia_experimento && d.energia_experimento.medicion ? d.energia_experimento.medicion.energia_wh : undefined;
        return e !== undefined ? e : d.col_energia_total_wh;
    }
    function accuracy(d) {
        if (d.col_accuracy_val !== null && d.col_accuracy_val !== undefined) return d.col_accuracy_val;
        return d.evaluacion ? d.evaluacion.val_accuracy : undefined;
    }
    // Inferencia (D-014): de la copia de metadata/resultados o, si no esta, de las columnas
    function conInferencia(d) { return (d.inferencia && d.inferencia.activada === true) || d.col_inferencia_activada === true; }
    function valorInferencia(d, clave, ventana) {
        if (ventana && d.energia_experimento && d.energia_experimento.inferencia && d.energia_experimento.inferencia[clave] !== undefined)
            return d.energia_experimento.inferencia[clave];
        if (!ventana && d.inferencia && d.inferencia[clave] !== undefined) return d.inferencia[clave];
        return d["col_" + clave];
    }
    // Metricas del modelo (D-015): de las columnas de la base o, si no estan, de la copia de
    // metricas_modelo.json. null si la corrida no las tiene (subida antes de la 009 o sin metricas)
    function metricaModelo(d, columna, conjunto, ruta) {
        var c = d["col_" + columna];
        if (c !== null && c !== undefined) return c;
        var m = d.metricas_modelo && d.metricas_modelo[conjunto] ? d.metricas_modelo[conjunto] : null;
        for (var i = 0; m && i < ruta.length; i++) m = m[ruta[i]];
        return m === null || m === undefined || typeof m === "object" ? null : m;
    }
    function f1MacroVal(d) { return metricaModelo(d, "f1_macro_val", "validacion", ["macro", "f1"]); }
    function exactitudEnt(d) { return metricaModelo(d, "accuracy_entrenamiento", "entrenamiento", ["exactitud"]); }
    // Exactitud en validacion: la de evaluar (accuracy_val), que coincide con la de las metricas del modelo
    function exactitudVal(d) {
        var a = root.accuracy(d);
        return a !== null && a !== undefined ? a : metricaModelo(d, "accuracy_val", "validacion", ["exactitud"]);
    }
    // Potencia media de la base previa (resultados.json), referencia del reposo (D-008)
    function potenciaBasePrevia(d) {
        return d.energia_experimento && d.energia_experimento.base_pre ? d.energia_experimento.base_pre.potencia_media_w : null;
    }
    readonly property string notaEnBruto: "En bruto: incluye el consumo del equipo en reposo (la corrección por línea base no está decidida, D-008)"
    function motivoInferencia(d) { return d.inferencia && d.inferencia.motivo_sin_por_prediccion ? d.inferencia.motivo_sin_por_prediccion : ""; }
    function hiper(d) {
        var l = [];
        for (var k in d.hiperparametros) l.push(k + "=" + d.hiperparametros[k]);
        return l.join(", ");
    }
    // Filas de la tabla: texto de cada corrida, agrupadas por secciones (D-002, D-003): equipo y
    // datos, experimento, desempeño del modelo, energía y recursos. Una fila con 'seccion' es un título.
    readonly property var filas: [
        { seccion: "Equipo y datos", color: "#8b949e" },
        { t: "Dueño", f: function (d) { return d.propia ? "Tú" : (d.dueno || "Otro usuario"); } },
        { t: "Equipo", f: function (d) { return d.equipo; } },
        { t: "CPU", f: function (d) { return d.hardware ? (d.hardware.cpu || "—") : "—"; } },
        { t: "Núcleos · RAM", f: function (d) { return d.hardware ? d.hardware.nucleos_logicos + " · " + root.num(d.hardware.ram_total_bytes / 1073741824, 1) + " GB" : "—"; } },
        { t: "Sistema operativo", f: function (d) { return d.hardware ? (d.hardware.sistema_operativo || "—") : "—"; } },
        { t: "Tipo de modelo", f: function (d) { return d.modelo_nombre || "—"; } },
        { t: "Datos", f: function (d) { return d.dataset ? d.dataset.nombre : (d.datos_origen === "sintetico" ? "Sintéticos" : "—"); } },
        { t: "Hiperparámetros", f: function (d) { return root.hiper(d) || "—"; } },
        { t: "Estado", f: function (d) { return d.estado; } },
        { seccion: "Experimento", color: "#8b949e" },
        { t: "Duración del entrenamiento", ayuda: "duracion", f: function (d) { return root.duracion(d.duracion_entrenamiento_s); } },
        { seccion: "Desempeño del modelo", color: "#e6edf3" },
        { t: "Exactitud en validación", ayuda: "val_accuracy", f: function (d) { var a = root.accuracy(d); return a === undefined || a === null ? "—" : root.num(a * 100, 2) + " %"; } },
        // Calidad del modelo (D-015), junto a la exactitud: sin metricas, "—"
        { t: "F1 macro en validación", f: function (d) { var f = root.f1MacroVal(d); return f === null ? "—" : root.num(f, 3); } },
        { t: "Exactitud en entrenamiento", f: function (d) { var a = root.exactitudEnt(d); return a === null ? "—" : root.num(a * 100, 2) + " %"; } },
        { t: "Entrenamiento − validación (sobreajuste)", f: function (d) {
            var e = root.exactitudEnt(d), v = root.exactitudVal(d);
            if (e === null || v === null || v === undefined) return "—";
            var dif = (e - v) * 100;
            return (dif >= 0 ? "+" : "") + root.num(dif, 2) + " puntos"; } },
        { seccion: "Métricas de energía · monitor físico de energía", color: "#22c55e" },
        { t: "Energía del entrenamiento", ayuda: "energia", f: function (d) { return root.num(root.energiaEnt(d), 4) + " Wh"; } },
        { t: "Energía de la medición", f: function (d) { return root.num(root.energiaMed(d), 4) + " Wh"; } },
        { t: "Energía por minuto de entrenamiento", f: function (d) {
            var e = root.energiaEnt(d), s = d.duracion_entrenamiento_s;
            return e !== undefined && e !== null && s > 0 ? root.num(e / (s / 60), 4) + " Wh/min" : "—"; } },
        { t: "Potencia media / máxima", ayuda: "potencia_w", f: function (d) { return root.num(d.col_potencia_media_w, 1) + " / " + root.num(d.col_potencia_max_w, 1) + " W"; } },
        // Referencia del reposo (D-008): sin restar nada
        { t: "Potencia media de la base previa", f: function (d) { return root.num(root.potenciaBasePrevia(d), 1) + " W"; } },
        { seccion: "Uso de recursos computacionales · monitor de software", color: "#3b82f6" },
        { t: "CPU media del proceso", ayuda: "cpu_proceso_pct", f: function (d) { return root.num(d.col_cpu_media_pct, 1) + " %"; } },
        { seccion: "Auditoría de la corrida", color: "#8b949e" },
        // Auditoria (D-012): al final, para no mover las filas anteriores
        { t: "Veredicto de la auditoría", f: function (d) {
            return ({ valida: "Válida", con_advertencias: "Con advertencias", no_valida: "No válida" })[d.veredicto] || "Sin auditoría"; } },
        { seccion: "Inferencia", color: "#c4b5fd" },
        // Inferencia (D-014): aparte del entrenamiento, al final
        { t: "Inferencia", f: function (d) {
            if (!root.conInferencia(d)) return "Sin inferencia";
            var inf = d.inferencia || {};
            return "Activada" + (inf.lote !== undefined ? " · lote " + inf.lote + " · mínimo " + inf.duracion_min_s + " s" : ""); } },
        { t: "Potencia media de la inferencia", f: function (d) {
            if (!root.conInferencia(d)) return "—";
            var e = d.ventanas && d.ventanas.inferencia ? d.ventanas.inferencia.energia : null;
            var v = e && e.potencia_w && e.potencia_w.n > 0 ? e.potencia_w.media : null;
            return root.num(v, 1) + " W"; } },
        { t: "Energía de la inferencia", f: function (d) {
            if (!root.conInferencia(d)) return "—";
            var e = d.energia_experimento && d.energia_experimento.inferencia ? d.energia_experimento.inferencia.energia_wh : d.col_energia_inferencia_wh;
            return root.num(e, 4) + " Wh"; } },
        { t: "Predicciones", f: function (d) {
            var n = root.valorInferencia(d, "n_predicciones", false);
            return root.conInferencia(d) && n !== null && n !== undefined ? String(n) : "—"; } },
        { t: "Energía por predicción", f: function (d) {
            if (!root.conInferencia(d)) return "—";
            if (root.motivoInferencia(d)) return "— (" + root.motivoInferencia(d) + ")";
            var e = root.valorInferencia(d, "energia_por_prediccion_j", true);
            return e === null || e === undefined ? "—" : Number(e).toExponential(3) + " J"; } },
        { t: "Tiempo por predicción", f: function (d) {
            if (!root.conInferencia(d)) return "—";
            if (root.motivoInferencia(d)) return "— (" + root.motivoInferencia(d) + ")";
            var t = root.valorInferencia(d, "tiempo_por_prediccion_us", false);
            return t === null || t === undefined ? "—" : Number(t).toPrecision(4) + " µs"; } }
    ]
    readonly property real energiaMax: {
        var m = 0;
        for (var i = 0; i < corridas.length; i++) { var e = energiaEnt(corridas[i]); if (e > m) m = e; }
        return m;
    }

    ColumnLayout {
        id: columna
        x: 24
        y: 20
        width: root.width - 48
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Enlace { text: "Base de datos"; icono: "atras"; onClicked: root.volverSolicitado() }
            Item { Layout.fillWidth: true }
            Text {
                objectName: "estadoComparacion"
                text: !root.remoto ? "Vista sin backend."
                    : root.remoto.estadoComparacion === "cargando" ? "Cargando las corridas…"
                    : root.remoto.estadoComparacion === "error" ? root.remoto.errorComparacion : ""
                color: root.remoto && root.remoto.estadoComparacion === "error" ? "#f85149" : "#8b949e"; font.pixelSize: Tipografia.cuerpo
            }
        }

        // Tabla lado a lado
        SectionCard {
            objectName: "tablaComparacion"
            visible: root.corridas.length > 0
            Layout.fillWidth: true
            title: "Comparación de " + root.corridas.length + " corridas"
            subtitle: "Valores durante el entrenamiento ([t_antes, t_despues]); la energía es la del experimento (dato derivado, D-008). "
                      + "Energías y energía por predicción en bruto (ver la nota al pie)."
            accent: "#3b82f6"

            // Encabezado: una columna por corrida
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.preferredWidth: 200 }
                Repeater {
                    model: root.corridas
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: root.colores[index % 4] }
                            Text {
                                text: modelData.experimento + " / " + modelData.corrida
                                color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                        Text {
                            text: "Ver detalle"; color: "#3b82f6"; font.pixelSize: Tipografia.auxiliar
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.verCorrida(modelData.id_remoto) }
                        }
                    }
                }
            }

            Repeater {
                model: root.filas
                delegate: ColumnLayout {
                    id: filaTabla
                    required property var modelData
                    readonly property var definicion: modelData
                    readonly property bool esSeccion: modelData.seccion !== undefined
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                    RowLayout {
                        visible: filaTabla.esSeccion
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 8
                        Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 12; radius: 2; color: filaTabla.esSeccion ? filaTabla.definicion.color : "transparent" }
                        Text { text: filaTabla.esSeccion ? filaTabla.definicion.seccion : ""; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                    }
                    RowLayout {
                        visible: !filaTabla.esSeccion
                        Layout.fillWidth: true
                        spacing: 10
                        RowLayout {
                            Layout.preferredWidth: 200
                            Layout.maximumWidth: 200
                            spacing: 4
                            Text { text: filaTabla.esSeccion ? "" : filaTabla.definicion.t; color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.maximumWidth: implicitWidth; Layout.fillWidth: true }
                            Ayuda { visible: filaTabla.definicion.ayuda !== undefined; texto: Ayudas.texto(filaTabla.definicion.ayuda || ""); tamano: 11 }
                            Item { Layout.fillWidth: true }
                        }
                        Repeater {
                            model: filaTabla.esSeccion ? [] : root.corridas
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: filaTabla.definicion.f(modelData)
                                color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
            Text {
                objectName: "notaEnBrutoComparar"
                text: "ⓘ " + root.notaEnBruto + ": energía del entrenamiento, de la medición y de la inferencia, y energía por "
                      + "predicción. La potencia media de la base previa es la referencia del reposo; no se resta. La diferencia "
                      + "entrenamiento − validación es de la exactitud: una diferencia grande indica sobreajuste."
                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        // Energía del entrenamiento en barras
        SectionCard {
            visible: root.corridas.length > 0 && root.energiaMax > 0
            Layout.fillWidth: true
            title: "Energía del entrenamiento"
            subtitle: "Wh por corrida, medida con el monitor físico de energía (las corridas sin él no aparecen)."
            ayuda: Ayudas.texto("energia")
            accent: root.appState ? root.appState.energyAccent : "#22c55e"

            Repeater {
                model: root.corridas
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    readonly property var energia: root.energiaEnt(modelData)
                    visible: energia !== undefined && energia !== null
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: modelData.experimento + " / " + modelData.corrida; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 200; elide: Text.ElideRight }
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 16
                        Rectangle {
                            height: parent.height; radius: 4
                            width: root.energiaMax > 0 ? Math.max(2, parent.width * (energia || 0) / root.energiaMax) : 0
                            color: root.colores[index % 4]
                        }
                    }
                    Text { text: root.num(energia, 4) + " Wh"; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 90 }
                }
            }
        }

        // Series superpuestas desde el inicio del entrenamiento
        SectionCard {
            visible: root.corridas.length > 0
            Layout.fillWidth: true
            title: "Durante el entrenamiento"
            subtitle: "Segundos desde t_antes, sobre la rejilla de 1 s. Los huecos sin datos no se interpolan."

            Text { text: "Potencia (W) · monitor físico de energía"; color: "#f59e0b"; font.pixelSize: Tipografia.cuerpo }
            LineChart {
                objectName: "graficaPotencia"
                Layout.fillWidth: true
                implicitHeight: 180
                unidad: "W"
                series: root.corridas.map(function (d, i) {
                    return { name: d.experimento + "/" + d.corrida, color: root.colores[i % 4],
                             values: d.serie_entrenamiento ? d.serie_entrenamiento.potencia_w : [] };
                })
            }
            Text { text: "CPU del proceso de ejecución (% del equipo) · monitor de software"; color: "#3b82f6"; font.pixelSize: Tipografia.cuerpo }
            LineChart {
                Layout.fillWidth: true
                implicitHeight: 180
                unidad: "%"
                series: root.corridas.map(function (d, i) {
                    return { name: d.experimento + "/" + d.corrida, color: root.colores[i % 4],
                             values: d.serie_entrenamiento ? d.serie_entrenamiento.cpu_proceso_pct : [] };
                })
            }
            Flow {
                Layout.fillWidth: true
                spacing: 14
                Repeater {
                    model: root.corridas
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        spacing: 6
                        Rectangle { implicitWidth: 10; implicitHeight: 3; color: root.colores[index % 4] }
                        Text { text: modelData.experimento + " / " + modelData.corrida + " · " + modelData.equipo; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar }
                    }
                }
            }
        }
    }
}
