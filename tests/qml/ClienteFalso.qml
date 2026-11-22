import QtQuick

// Sustituto de ClienteOrquestador (src/ui/backend) para probar las vistas QML sin lanzar
// procesos. Expone las mismas propiedades y funciones que usan las vistas y registra las
// llamadas para comprobarlas.
//
// El modelo "prueba" reproduce la descripcion que declara la DLL de prueba
// (src/execution/models/prueba/energia_dll_stub.cpp) tal como la serializa
// CatalogoModelos::modeloAJson(). El modelo "ficticio" solo existe en estas pruebas.
QtObject {
    id: falso

    readonly property var modeloPrueba: ({
        archivo: "EnergIA_Prueba.dll", ruta: "C:\\energia\\bin\\EnergIA_Prueba.dll",
        clave: "prueba", nombre: "Prueba (simulado)", libreria: "ninguna (carga de CPU simulada)",
        unidad_paso: "paso", hilos: 1, interno: true,
        hiperparametros: [
            { clave: "pasos", etiqueta: "Pasos", ayuda: "Cuantos pasos simula (como epocas o arboles)",
              tipo: "entero", minimo: 1, maximo: 1000, por_defecto: 10 },
            { clave: "ms_por_paso", etiqueta: "Duracion por paso (ms)", ayuda: "Milisegundos de carga real de CPU por paso",
              tipo: "entero", minimo: 50, maximo: 60000, por_defecto: 500 },
            { clave: "loss_final", etiqueta: "Loss final", ayuda: "Valor al que tiende la train_loss simulada",
              tipo: "real", minimo: 0.0, maximo: 5.0, por_defecto: 0.2 },
            { clave: "curva", etiqueta: "Forma de la curva", ayuda: "Como baja la train_loss simulada",
              tipo: "opcion", opciones: ["exponencial", "lineal"], por_defecto: 0 },
            { clave: "capas", etiqueta: "Capas (prueba)", ayuda: "Solo para probar el tipo lista; no afecta la simulacion",
              tipo: "lista_enteros", minimo: 1, maximo: 4096, max_elementos: 8, por_defecto: "64,32" }
        ]
    })
    readonly property var modeloFicticio: ({
        archivo: "EnergIA_Ficticio.dll", ruta: "C:\\energia\\bin\\EnergIA_Ficticio.dll",
        clave: "ficticio", nombre: "Modelo ficticio de pruebas", libreria: "ninguna",
        unidad_paso: "epoca", hilos: 1, interno: false,
        hiperparametros: [
            { clave: "profundidad", etiqueta: "Profundidad", ayuda: "", tipo: "entero", minimo: 1, maximo: 50, por_defecto: 5 }
        ]
    })

    // --- Propiedades que usan las vistas ---
    property string rutaOrquestador: "C:\\energia\\bin\\energia_orquestador.exe"
    property string dirModelos: "C:\\energia\\bin"
    property string dirSalida: "C:\\energia\\salida"
    property string dirDatasets: "C:\\energia\\datasets"
    property bool modoDesarrollo: true
    property bool orquestadorEncontrado: true
    property int lineaBaseSegundos: 20

    property string estadoCatalogo: "listo"
    property string errorCatalogo: ""
    property var modelos: [modeloPrueba, modeloFicticio]
    property var descartadas: []
    property int modelosInternosOcultos: 0

    property string estado: "inactivo"
    property bool activa: false
    property string fase: ""
    property var corrida: ({})
    property int pasosCompletados: 0
    property int pasoActual: 0
    property int totalPasos: 0
    property string unidadPaso: ""
    property var metricasPaso: ({})
    property var progresoInferencia: ({})
    property var registro: []
    property var medidoresCorrida: []
    property var metricasSistema: ({})
    property var historialSistema: ({})
    property var metricasEnergia: ({})
    property var historialEnergia: ({})
    property var laboratorio: ({ mac: "00:00:00:19:EC:2C", python: "C:\\lab\\venv\\Scripts\\python.exe", python_existe: true,
                                 script: "C:\\lab\\energia_ble.py", script_existe: true })

    // --- Registro de llamadas ---
    property var ultimaSeleccion: null
    property string respuestaIniciar: ""
    property int llamadasCatalogo: 0
    property int llamadasCancelar: 0
    property int llamadasForzar: 0
    property int llamadasDescartar: 0
    property var encabezado: ({ ok: true, columnas: ["x1", "x2", "etiqueta"], separador: ";", error: "" })

    signal notificacion(string tipo, string titulo, string descripcion)

    function cargarCatalogo() { llamadasCatalogo++; }
    function iniciar(seleccion) { ultimaSeleccion = seleccion; return respuestaIniciar; }
    function cancelar() { llamadasCancelar++; }
    function forzarDetencion() { llamadasForzar++; }
    function descartarResultado() { llamadasDescartar++; }
    function encabezadoCsv(ruta) { return encabezado; }
    function rutaLocal(url) { return String(url); }
    function urlCarpeta(ruta) { return "file:///" + ruta.replace(/\\\\/g, "/"); }
    function configurarCarpeta(tipo, ruta) {
        if (activa) return "Hay una corrida en curso.";
        var p = String(ruta);
        if (tipo === "resultados") dirSalida = p;
        else if (tipo === "modelos") dirModelos = p;
        else if (tipo === "datasets") dirDatasets = p;
        else return "Tipo desconocido";
        return "";
    }
    function textoFase(f) { return "fase:" + f; }
    function medidorEnergiaLaboratorio() { return laboratorio; }
    property bool systemMeterInstalado: true
    function disponibilidadMonitores() {
        return { sistema: systemMeterInstalado, energia: laboratorio.python_existe && laboratorio.script_existe };
    }
    function infoSistema() {
        return { sistema_operativo: "SO de prueba", cpu: "CPU de prueba", nucleos_logicos: 4, memoria_total_gb: 8, arquitectura: "x86_64", version_qt: "6.11.2" };
    }
}
