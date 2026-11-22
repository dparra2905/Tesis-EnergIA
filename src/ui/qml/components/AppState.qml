import QtQuick

// Estado visual compartido de la interfaz. No contiene datos de demostración:
// - el estado de cada monitor lo fija Main.qml a partir de la corrida en curso y de la
//   disponibilidad real (instalado/configurado); nunca afirma una conexión que no se verificó;
// - las notificaciones llegan del proceso de ejecución mediante agregarNotificacion().
QtObject {
    id: root

    // --- Monitores ---

    // Monitor de software: uso de recursos computacionales (CPU, RAM).
    // softwareMonitorEnabled: incluirlo por defecto en los nuevos experimentos.
    property bool softwareMonitorEnabled: true
    property string softwareMonitorStatus: "Sin verificar"
    property color softwareMonitorColor: "#8b949e"

    // Monitor físico de energía: potencia, energía, voltaje, corriente.
    // Desactivado por defecto: sin el Atorch encendido la corrida se cancela en t_start.
    property bool energyMonitorEnabled: false
    property string energyMonitorStatus: "Sin verificar"
    property color energyMonitorColor: "#8b949e"

    // --- Métricas: dos grupos separados, cada uno asociado a su monitor ---

    readonly property color resourcesAccent: "#3b82f6"
    readonly property color energyAccent: "#22c55e"

    // icon: nombre del icono en Iconos.js
    readonly property var resourceMetrics: [
        { key: "cpu", label: "CPU", unit: "%", icon: "cpu", color: "#3b82f6" },
        { key: "ram", label: "RAM", unit: "%", icon: "memoria", color: "#a78bfa" }
    ]

    readonly property var energyMetrics: [
        { key: "potencia", label: "Potencia", unit: "W", icon: "enchufe", color: "#f59e0b" },
        { key: "energia", label: "Energía", unit: "Wh", icon: "rayo", color: "#22c55e" },
        { key: "voltaje", label: "Voltaje", unit: "V", icon: "voltaje", color: "#22d3ee" },
        { key: "corriente", label: "Corriente", unit: "A", icon: "corriente", color: "#f472b6" }
    ]

    function metric(key) {
        var all = resourceMetrics.concat(energyMetrics);
        for (var i = 0; i < all.length; i++)
            if (all[i].key === key) return all[i];
        return { key: key, label: key, unit: "", icon: "", color: "#8b949e" };
    }

    // --- Corrida activa ---

    // Regla única: mientras una corrida está activa (desde que se lanza el orquestador hasta su
    // fin) no se exporta el modelo ni el CSV de la serie, no se sube a la base ni se calcula la
    // auditoría desde la interfaz: compiten por CPU y disco con la medición. Main.qml la enlaza a
    // cliente.activa; cada una de esas acciones la consulta (enabled: !appState.corridaActiva) y
    // muestra avisoCorridaActiva. La cola de subidas se pausa por su lado (ColaSubidas::setCorridaActiva).
    // Leer datos para mostrar (listas, detalle, gráficas) sigue permitido.
    property bool corridaActiva: false
    readonly property string avisoCorridaActiva: "Disponible cuando termine la corrida en curso"

    // --- Centro de notificaciones ---

    property int unreadCount: 0

    property ListModel notifications: ListModel {}

    // Notificación real (por ejemplo, del proceso de ejecución). Se inserta la primera.
    // Con 'dirDecision', la notificación lleva las acciones de una corrida que espera decisión
    // (modo de subida, D-012): Subir, Subir con nota y No subir (ver decisionSolicitada).
    function agregarNotificacion(kind, title, description, dirDecision) {
        var ahora = new Date();
        var hh = ahora.getHours(), mm = ahora.getMinutes();
        notifications.insert(0, {
            kind: kind, title: title, description: description,
            time: "Hoy " + (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm,
            read: false, dir: dirDecision || "", resuelta: false
        });
    }

    // Acción de una notificación con decisión: "subir" | "subir_con_nota" | "no_subir"
    signal decisionSolicitada(string dir, string accion)
    function decidir(index, accion) {
        if (index < 0 || index >= notifications.count) return;
        var n = notifications.get(index);
        notifications.setProperty(index, "resuelta", true);
        notifications.setProperty(index, "read", true);
        recountUnread();
        decisionSolicitada(n.dir, accion);
    }
    // La decisión se tomó en otra parte (Experimento en ejecución, Resultados, Base de datos)
    function decisionTomada(dir) {
        for (var i = 0; i < notifications.count; i++)
            if (notifications.get(i).dir === dir) notifications.setProperty(i, "resuelta", true);
    }

    function recountUnread() {
        var n = 0;
        for (var i = 0; i < notifications.count; i++)
            if (!notifications.get(i).read) n++;
        unreadCount = n;
    }

    function markRead(index) {
        if (index < 0 || index >= notifications.count) return;
        notifications.setProperty(index, "read", true);
        recountUnread();
    }

    function markAllRead() {
        for (var i = 0; i < notifications.count; i++)
            notifications.setProperty(i, "read", true);
        recountUnread();
    }

    property Connections notificationsWatcher: Connections {
        target: root.notifications
        function onCountChanged() { root.recountUnread() }
    }

    Component.onCompleted: recountUnread()
}
