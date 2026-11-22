import QtQuick

// Sustituto de ColaSubidas (src/ui/backend) para probar las vistas sin red (DATOS INVENTADOS)
QtObject {
    property var elementos: []
    readonly property int pendientes: elementos.filter(function (e) { return e.estado === "pendiente" || e.estado === "subiendo"; }).length
    readonly property int rechazadas: elementos.filter(function (e) { return e.estado === "rechazada"; }).length
    readonly property int sinCuenta: elementos.filter(function (e) { return e.propietario === "sin_cuenta" && e.estado !== "subida"; }).length
    readonly property int esperandoDecision: elementos.filter(function (e) { return e.estado === "esperando_decision"; }).length
    property bool subiendo: false
    property int reclamos: 0
    property var reintentos: []
    // Modo de subida y decisiones (D-012)
    property string modoSubida: "subir_todo"
    property string equipo: "PC-ANA"
    property var decisiones: []
    signal notificacion(string tipo, string titulo, string descripcion)
    signal decisionPendiente(string dir, string nombre, string veredicto)

    function decidir(dir, subir) {
        decisiones = decisiones.concat([{ dir: dir, subir: subir }]);
        var l = elementos.filter(function (e) { return e.dir !== dir; });
        elementos = subir ? l.concat([{ dir: dir, estado: "pendiente", ultimo_error: "" }]) : l;
    }

    function estadoDe(dir) {
        for (var i = 0; i < elementos.length; i++) if (elementos[i].dir === dir) return elementos[i];
        return { estado: "no_encolada" };
    }
    function reintentar(dir) {
        reintentos = reintentos.concat([dir]);
        var l = elementos.filter(function (e) { return e.dir !== dir; });
        elementos = l.concat([{ dir: dir, estado: "pendiente", ultimo_error: "" }]);
    }
    function reintentarTodo() { reintentos = reintentos.concat(["*"]); }
    function reclamarSinCuenta() { reclamos++; }
}
