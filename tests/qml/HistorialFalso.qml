import QtQuick

// Sustituto de Historial (src/ui/backend) para probar Dashboard y Resultados sin archivos.
// Los datos son inventados para la prueba, con la forma que devuelve Historial.
QtObject {
    id: falso

    property string dirSalida: "C:\\energia\\salida"
    property int llamadasRecargar: 0
    property var ultimaExportacion: null
    property string respuestaExportar: ""

    readonly property var corridaConMedidores: ({
        dir: "C:\\salida\\corridas\\exp_a\\corrida_002", id: "exp_a/corrida_002", experimento: "exp_a", corrida: 2,
        estado: "completada", error: "", modelo_nombre: "Prueba (simulado)", modelo_clave: "prueba", modelo_interno: true,
        unidad_paso: "paso", datos_origen: "sintetico", datos_ruta: "", n_train: 1200, n_val: 300,
        hiperparametros: { pasos: 4 }, pasos_registrados: 4, evaluacion: { val_accuracy: 0.8 },
        t_creacion: "2026-10-01T10:00:00.000Z", t_start: "2026-10-01T10:00:02.000Z", t_antes: "2026-10-01T10:00:04.000Z",
        t_despues: "2026-10-01T10:00:08.000Z", t_stop: "2026-10-01T10:00:10.000Z", duracion_entrenamiento_s: 4,
        medidores: ["sistema", "energia"], metodo_energia: "trapecio_tramas_unicas", version_metodo_energia: 1,
        energia_experimento: { entrenamiento: { energia_wh: 0.0444444, cobertura: 1, duracion_s: 4, huecos: [] } },
        veredicto: "con_advertencias", nota: "",
        auditoria: { version_reglas: 1, veredicto: "con_advertencias", comprobaciones: [
            { id: "estado", descripcion: "Estado final", valor: "completada", unidad: null, umbral: "completada = ok",
              resultado: "ok", motivo: "corrida completada" },
            { id: "actividad_ajena", descripcion: "Minutos con CPU ajena", valor: 15.2, unidad: "% de los minutos",
              umbral: "ok <= 10.0 %", resultado: "advertencia", motivo: "entrenamiento: 10 de 66 minutos (15.2 %)" },
            { id: "caidas_ble", descripcion: "Caídas de la conexión Bluetooth", valor: null, unidad: "caídas",
              umbral: "ok de 0 a 2; advertencia >= 3", resultado: "no_evaluable", motivo: "la corrida no incluyó el monitor físico de energía" }] }
    })
    readonly property var corridaSinMedidores: ({
        dir: "C:\\salida\\corridas\\exp_b\\corrida_001", id: "exp_b/corrida_001", experimento: "exp_b", corrida: 1,
        estado: "cancelada", error: "", modelo_nombre: "Prueba (simulado)", modelo_clave: "prueba", modelo_interno: true,
        unidad_paso: "paso", datos_origen: "sintetico", hiperparametros: {}, pasos_registrados: 1, evaluacion: {},
        t_creacion: "2026-10-01T09:00:00.000Z", t_start: "2026-10-01T09:00:01.000Z", duracion_entrenamiento_s: null,
        medidores: []
    })

    // Corridas con inferencia (D-014): una completa con energia y otra cancelada en la inferencia
    readonly property var corridaConInferencia: ({
        dir: "C:\\salida\\corridas\\exp_inf\\corrida_001", id: "exp_inf/corrida_001", experimento: "exp_inf", corrida: 1,
        estado: "completada", error: "", modelo_nombre: "MLP (perceptron multicapa)", modelo_clave: "mlp", modelo_interno: false,
        unidad_paso: "epoca", datos_origen: "sintetico", datos_ruta: "", n_train: 1200, n_val: 300,
        hiperparametros: { epocas: 2 }, pasos_registrados: 2, evaluacion: { val_accuracy: 0.9 },
        t_creacion: "2026-10-01T08:00:00.000Z", t_start: "2026-10-01T08:00:02.000Z", t_antes: "2026-10-01T08:00:04.000Z",
        t_despues: "2026-10-01T08:00:08.000Z", t_inf_antes: "2026-10-01T08:00:10.000Z", t_inf_despues: "2026-10-01T08:01:10.400Z",
        t_stop: "2026-10-01T08:01:12.000Z", duracion_entrenamiento_s: 4, medidores: ["sistema", "energia"],
        metodo_energia: "trapecio_tramas_unicas", version_metodo_energia: 1,
        inferencia: { activada: true, lote: 65536, duracion_min_s: 60, n_filas: 300, status_dll: 0, status_texto: "ok",
                      grupos_registrados: 60, pasadas: 80000, n_predicciones: 24000000, duracion_ms: 60400,
                      tiempo_por_prediccion_us: 2.5167, motivo_sin_por_prediccion: null },
        energia_experimento: { entrenamiento: { energia_wh: 0.0444444, cobertura: 1 },
                               base_media: { energia_wh: 0.0111111, cobertura: 1 },
                               inferencia: { energia_wh: 0.75, cobertura: 1, n_predicciones: 24000000,
                                             energia_por_prediccion_j: 0.0001125, tiempo_por_prediccion_us: 2.5167,
                                             motivo_sin_por_prediccion: null } },
        veredicto: "valida", nota: "", auditoria: {}
    })
    readonly property var corridaInferenciaCancelada: ({
        dir: "C:\\salida\\corridas\\exp_inf\\corrida_002", id: "exp_inf/corrida_002", experimento: "exp_inf", corrida: 2,
        estado: "cancelada", error: "", modelo_nombre: "MLP (perceptron multicapa)", modelo_clave: "mlp", modelo_interno: false,
        unidad_paso: "epoca", datos_origen: "sintetico", hiperparametros: {}, pasos_registrados: 2, evaluacion: {},
        t_creacion: "2026-10-01T07:00:00.000Z", t_start: "2026-10-01T07:00:01.000Z", duracion_entrenamiento_s: 4,
        medidores: ["energia"],
        inferencia: { activada: true, lote: 1, duracion_min_s: 60, status_dll: 4, status_texto: "cancelado", pasadas: 10,
                      n_predicciones: 3000, tiempo_por_prediccion_us: null, motivo_sin_por_prediccion: "inferencia cancelada" },
        energia_experimento: { inferencia: { energia_wh: 0.01, n_predicciones: 3000, energia_por_prediccion_j: null,
                                             tiempo_por_prediccion_us: null, motivo_sin_por_prediccion: "inferencia cancelada" } }
    })

    // Metricas del modelo (D-015), con la forma de metricas_modelo.json: validacion con una clase que
    // nunca se predice (precision indefinida) y entrenamiento en que predecir fallo
    readonly property var metricasModelo: ({
        version_formato: 1, clases_originales: ["no", "si"], n_clases: 2,
        validacion: { status_dll: 0, motivo: null, n_filas: 300, duracion_ms: 2, duracion_predecir_ms: 1, exactitud: 0.8,
                      matriz_confusion: [[240, 0], [60, 0]],
                      por_clase: [
                          { indice: 0, etiqueta: "no", soporte: 240, precision: { valor: 0.8, indefinida: false },
                            recall: { valor: 1, indefinida: false }, f1: { valor: 0.8889, indefinida: false } },
                          { indice: 1, etiqueta: "si", soporte: 60, precision: { valor: 0, indefinida: true },
                            recall: { valor: 0, indefinida: false }, f1: { valor: 0, indefinida: false } }],
                      macro: { precision: 0.4, recall: 0.5, f1: 0.4444 }, ponderado: { precision: 0.64, recall: 0.8, f1: 0.7111 } },
        entrenamiento: { status_dll: 3, motivo: "predecir devolvio: error interno de la DLL", n_filas: 1200, matriz_confusion: null,
                         exactitud: null, por_clase: null, macro: null, ponderado: null }
    })

    property var corridas: [corridaConMedidores, corridaSinMedidores]

    function recargar() { llamadasRecargar++; }
    // Nota de condiciones (D-012)
    property var notasGuardadas: []
    function nota(dir) { return ""; }
    function guardarNota(dir, texto) { notasGuardadas = notasGuardadas.concat([{ dir: dir, texto: texto }]); return ""; }
    function rutaLocal(url) { return String(url); }
    function exportarCsv(dir, destino) { ultimaExportacion = { dir: dir, destino: destino }; return respuestaExportar; }
    // Exportar el modelo (D-013): la primera corrida es exportable (MLP con columnas conocidas)
    property var infoModelos: ({})
    property var exportacionesModelo: []
    property bool exportandoModelo: false
    signal modeloExportado(string dir, string destino, string error, var avisos)
    function infoExportacion(dir) {
        if (infoModelos[dir] !== undefined) return infoModelos[dir];
        return { exportable: false, motivo: "La corrida no guardó modelo.bin", avisos: [] };
    }
    function exportarModelo(dir, destino) { exportacionesModelo = exportacionesModelo.concat([{ dir: dir, destino: destino }]); }
    function detalle(dir) {
        for (var i = 0; i < corridas.length; i++) {
            if (corridas[i].dir !== dir) continue;
            var d = JSON.parse(JSON.stringify(corridas[i]));
            // Como Historial::detalle: metricas_modelo solo en el detalle ({} sin el archivo)
            d.metricas_modelo = d.dir === corridaConMedidores.dir ? metricasModelo : {};
            if (d.medidores.length > 0) {
                d.ventanas = { entrenamiento: {
                    sistema: { cpu_proceso_pct: { n: 4, min: 8, max: 12.5, media: 12.5 }, cpu_sistema_pct: { n: 4, min: 20, max: 30, media: 25 },
                               ram_proceso_mb: { n: 4, min: 100, max: 200, media: 150 }, ram_sistema_pct: { n: 4, min: 50, max: 50, media: 50 } },
                    energia: { potencia_w: { n: 5, min: 39, max: 41, media: 40 }, voltaje_v: { n: 5, min: 220, max: 220, media: 220 },
                               corriente_a: { n: 5, min: 0.18, max: 0.186, media: 0.182 }, filas: 5, filas_con_trama: 5 } } };
                if (d.energia_experimento) d.energia_experimento.base_pre = { energia_wh: 0.0011, cobertura: 1, potencia_media_w: 18.5 };
                d.series_energia = { t_s: [0, 1, 2, 3], potencia_w: [20, 40, null, 40] };
                d.series_sistema = { t_s: [1, 2, 3], cpu_proceso_pct: [0, 12.5, 12.5],
                                      ram_proceso_mb: [100, 150, null], ram_sistema_pct: [49, 50, 51] };
                d.t_antes_rel_s = 2;
                d.t_despues_rel_s = 6;
            } else {
                d.ventanas = {};
            }
            return d;
        }
        return {};
    }
}
