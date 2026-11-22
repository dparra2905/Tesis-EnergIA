import QtQuick

// Sustituto de ClienteSupabase (src/ui/backend) para probar la pantalla de acceso y la barra
// superior sin red. Cuenta valida: ana@ejemplo.com / secreta1 (DATOS INVENTADOS).
QtObject {
    id: falsa

    property string estado: "sin_sesion"
    readonly property bool conectado: estado === "activa"
    readonly property bool puedeEntrar: estado === "activa" || estado === "sin_conexion" || estado === "local"
    property string usuarioId: ""
    property string correo: ""
    property string nombre: ""
    property string rol: ""
    readonly property bool esAdmin: rol === "admin"
    property bool ocupado: false
    property string error: ""
    property string aviso: ""
    property bool errorDeRed: false
    // Simula la falta de red en el proximo login
    property bool sinRed: false
    property var registros: []
    property int cierres: 0

    signal mensajeCambiado()
    signal cuentaActualizada(bool ok, string mensaje)
    signal codigoEnviado(string correo)
    property var recuperaciones: []
    onErrorChanged: mensajeCambiado()
    onAvisoChanged: mensajeCambiado()

    function iniciarSesion(c, p) {
        error = ""; aviso = ""; errorDeRed = false;
        if (sinRed) { errorDeRed = true; error = "Sin conexión con la base de datos."; return; }
        if (c === "ana@ejemplo.com" && p === "secreta1") {
            usuarioId = "u-ana"; correo = c; nombre = "Ana Pérez"; rol = "investigador"; estado = "activa";
        } else {
            error = "Correo o contraseña incorrectos.";
        }
    }
    function registrar(c, p, n) {
        registros = registros.concat([{ correo: c, contrasena: p, nombre: n }]);
        error = "";
        aviso = "Cuenta creada. Revisa " + c + " y abre el enlace de confirmación; después inicia sesión.";
    }
    function cerrarSesion() { cierres++; estado = "sin_sesion"; usuarioId = ""; correo = ""; nombre = ""; rol = ""; }
    function continuarSinConexion() { estado = "local"; }
    function cambiarNombre(n) { nombre = n.trim(); cuentaActualizada(true, "Nombre actualizado."); }
    function cambiarContrasena(a, n) {
        if (a !== "secreta1") cuentaActualizada(false, "La contraseña actual no es correcta.");
        else cuentaActualizada(true, "Contraseña cambiada.");
    }
    function solicitarRecuperacion(c) { aviso = "Si " + c + " tiene una cuenta, te llegó un código."; codigoEnviado(c); }
    function restablecerContrasena(c, codigo, n) {
        recuperaciones = recuperaciones.concat([{ correo: c, codigo: codigo, nueva: n }]);
        if (codigo === "123456") { correo = c; usuarioId = "u-ana"; estado = "activa"; }
        else error = "El código no es válido o ya venció. Pide uno nuevo.";
    }
    function limpiarMensajes() { error = ""; aviso = ""; errorDeRed = false; }
}
