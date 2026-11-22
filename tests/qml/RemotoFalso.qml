import QtQuick

// Sustituto de HistorialRemoto (src/ui/backend) para probar las páginas Base de datos,
// Resultados (modo remoto) y Comparar sin red. Los datos son inventados para la prueba.
QtObject {
    property string estado: "listo"
    property string error: ""
    property real exportando: 0
    property int recargas: 0
    property var cambios: []
    property var exportaciones: []
    property var detallesPedidos: []
    property var borrados: []
    property var comparacionesPedidas: []
    property var corridas: [
        { id: 1, experimento: "exp_a", numero: 1, equipo: "PC-ANA", estado: "completada", modelo_dll: "EnergIA_Arbol.dll",
          visibilidad: "privada", propia: true, dueno: "Ana", subida_en: "2026-10-14T12:00:00+00:00",
          energia_entrenamiento_wh: 7.0939, accuracy_val: 0.83, veredicto: "valida" },
        { id: 2, experimento: "mlp", numero: 4, equipo: "PC-BETO", estado: "completada", modelo_dll: "EnergIA_MLP.dll",
          visibilidad: "compartida", propia: false, dueno: "Beto", subida_en: "2026-10-13T12:00:00+00:00",
          energia_entrenamiento_wh: null, accuracy_val: null, veredicto: "" }
    ]

    property var detalle: ({})
    property string estadoDetalle: "inactivo"
    property string errorDetalle: ""
    property var comparacion: []
    property string estadoComparacion: "inactivo"
    property string errorComparacion: ""

    signal visibilidadCambiada(real id, string visibilidad, string error)
    signal exportacionTerminada(real id, string destino, int filas, string error)
    signal corridaBorrada(real id, string error)
    signal notaCambiada(real id, string nota, string error)
    // Nota (D-012): la prueba decide la respuesta
    property var notasPedidas: []
    property string errorNota: ""
    function cambiarNota(id, nota) { notasPedidas = notasPedidas.concat([{ id: id, nota: nota }]); notaCambiada(id, nota, errorNota); }

    // Detalle con la forma de HistorialRemoto.detalle
    function detalleDe(id) {
        var propia = id === 1;
        return {
            dir: "base:" + id, origen: "base", id_remoto: id, experimento: propia ? "exp_a" : "mlp", corrida: propia ? 1 : 4,
            equipo: propia ? "PC-ANA" : "PC-BETO", propia: propia, dueno: propia ? "Ana" : "Beto",
            visibilidad: propia ? "privada" : "compartida", subida_en: "2026-10-14T12:00:00+00:00",
            veredicto: propia ? "valida" : "", nota: propia ? "Sala a 24 grados" : "", t_creacion: "2026-10-01T09:59:00.000Z",
            estado: "completada", error: "", modelo_nombre: propia ? "Arbol de decision" : "MLP", modelo_clave: "arbol_decision",
            modelo_interno: false, unidad_paso: "arbol", datos_origen: "csv", datos_ruta: "C:\\datos\\hepmass.csv",
            n_train: 1200, n_val: 300, hiperparametros: { criterio: "gini" }, pasos_registrados: 1, evaluacion: { val_accuracy: 0.83 },
            t_start: "2026-10-01T10:00:00.000Z", t_antes: "2026-10-01T10:00:02.000Z", t_despues: "2026-10-01T10:00:06.000Z",
            t_stop: "2026-10-01T10:00:08.000Z", duracion_entrenamiento_s: propia ? 240 : 600, medidores: ["sistema", "energia"],
            hardware: { cpu: "CPU de prueba", nucleos_logicos: 8, ram_total_bytes: 8589934592, sistema_operativo: "Windows 11" },
            dataset: { nombre: "hepmass.csv", sha256: "abcdef0123456789abcdef" },
            energia_experimento: { entrenamiento: { energia_wh: propia ? 2.5 : 5.0, cobertura: 1 },
                                   base_pre: { energia_wh: 0.1, cobertura: 1, potencia_media_w: propia ? 18.5 : 21 } },
            metodo_energia: "trapecio_tramas_unicas", version_metodo_energia: 1,
            ventanas: { entrenamiento: {
                sistema: { cpu_proceso_pct: { n: 4, min: 10, max: 15, media: 12.5 }, cpu_sistema_pct: { n: 4, min: 20, max: 30, media: 25 },
                           ram_proceso_mb: { n: 4, min: 200, max: 200, media: 200 }, ram_sistema_pct: { n: 4, min: 50, max: 50, media: 50 } },
                energia: { potencia_w: { n: 5, min: 40, max: 45.5, media: 41 }, voltaje_v: { n: 5, min: 220, max: 220, media: 220 },
                           corriente_a: { n: 5, min: 0.18, max: 0.2, media: 0.19 } } } },
            series_sistema: { t_s: [1, 2, 3], cpu_proceso_pct: [0, 12.5, 12.5] },
            series_energia: { t_s: [0, 1, 2], potencia_w: [20, 40, 45.5] },
            t_antes_rel_s: 2, t_despues_rel_s: 6,
            col_energia_entrenamiento_wh: propia ? 2.5 : 5.0, col_potencia_media_w: 41, col_potencia_max_w: 45.5,
            col_cpu_media_pct: 12.5, col_accuracy_val: 0.83,
            // Metricas del modelo (D-015): la corrida 1 las tiene; la 2 se subio antes de la 009
            col_f1_macro_val: propia ? 0.8123 : null, col_accuracy_entrenamiento: propia ? 0.95 : null,
            metricas_modelo: propia ? {
                version_formato: 1, clases_originales: [0, 1, 2],
                validacion: { status_dll: 0, motivo: null, n_filas: 300, exactitud: 0.83,
                              matriz_confusion: [[90, 5, 5], [10, 80, 10], [5, 16, 79]],
                              por_clase: [
                                  { indice: 0, etiqueta: 0, soporte: 100, precision: { valor: 0.857, indefinida: false },
                                    recall: { valor: 0.9, indefinida: false }, f1: { valor: 0.878, indefinida: false } },
                                  { indice: 1, etiqueta: 1, soporte: 100, precision: { valor: 0.792, indefinida: false },
                                    recall: { valor: 0.8, indefinida: false }, f1: { valor: 0.796, indefinida: false } },
                                  { indice: 2, etiqueta: 2, soporte: 100, precision: { valor: 0.840, indefinida: false },
                                    recall: { valor: 0.79, indefinida: false }, f1: { valor: 0.814, indefinida: false } }],
                              macro: { precision: 0.83, recall: 0.83, f1: 0.8123 }, ponderado: { precision: 0.83, recall: 0.83, f1: 0.8123 } },
                entrenamiento: { status_dll: 0, motivo: null, n_filas: 1200, exactitud: 0.95,
                                 matriz_confusion: [[390, 5, 5], [10, 380, 10], [5, 25, 370]], por_clase: [],
                                 macro: { precision: 0.95, recall: 0.95, f1: 0.95 }, ponderado: { precision: 0.95, recall: 0.95, f1: 0.95 } }
            } : {},
            serie_entrenamiento: { potencia_w: [40, 41, 45.5], cpu_proceso_pct: [12.5, 12.5, 12.5] }
        };
    }

    function recargar() { recargas++; }
    function cambiarVisibilidad(id, v) { cambios = cambios.concat([{ id: id, visibilidad: v }]); visibilidadCambiada(id, v, ""); }
    function exportarCsv(id, destino) { exportaciones = exportaciones.concat([{ id: id, destino: destino }]); }
    function rutaLocal(url) { return String(url); }
    function cargarDetalle(id) { detallesPedidos = detallesPedidos.concat([id]); estadoDetalle = "cargando"; }
    // La prueba decide cuándo "llega" el detalle
    function entregarDetalle(id) { detalle = detalleDe(id); estadoDetalle = "listo"; }
    function borrar(id) { borrados = borrados.concat([id]); }
    function cargarComparacion(ids) {
        comparacionesPedidas = comparacionesPedidas.concat([ids]);
        comparacion = ids.map(function (i) { return detalleDe(i); });
        estadoComparacion = "listo";
    }
}
