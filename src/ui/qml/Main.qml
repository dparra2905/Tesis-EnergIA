import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"
import "components/Tipografia.js" as Tipografia

ApplicationWindow {
    id: window
    width: 1440
    height: 860
    minimumWidth: 1150
    minimumHeight: 720
    visible: true
    title: "EnergIA Monitor"
    color: "#0d1117"

    // Paleta oscura comun (components/PaletaOscura.qml): un solo lugar para todos los controles
    palette: PaletaOscura {}
    font.pixelSize: Tipografia.cuerpo

    property string currentPage: "dashboard"
    property AppState appState: AppState {}
    // ClienteOrquestador (C++), entregado por main.cpp. Es null si la interfaz se abre sin
    // backend (por ejemplo, con qml.exe): las vistas lo indican en lugar de simular datos.
    property var cliente: null
    // Historial de corridas (C++), entregado por main.cpp; null sin backend
    property var historial: null
    // Corrida que muestra la página de resultados ("" = la más reciente)
    property string corridaSeleccionada: ""
    // Sesión de la base de datos (ClienteSupabase, D-011), entregada por main.cpp; null sin backend
    property var sesion: null
    // Cola de subidas a la base (ColaSubidas, D-011), entregada por main.cpp; null sin backend
    property var subidas: null
    // Corridas de la base de datos (HistorialRemoto, D-011), entregado por main.cpp; null sin backend
    property var remoto: null

    readonly property string inicialesUsuario: {
        if (!sesion) return "ML";
        var base = sesion.nombre !== "" ? sesion.nombre : sesion.correo;
        if (base === "") return "—";
        var partes = base.split(/[\s@._-]+/).filter(function (p) { return p.length > 0; });
        return (partes.length > 1 ? partes[0][0] + partes[1][0] : base.substring(0, 2)).toUpperCase();
    }
    readonly property string textoEstadoSesion: {
        if (!sesion) return "";
        switch (sesion.estado) {
        case "activa": return sesion.esAdmin ? "Conectado a la base de datos (administrador)" : "Conectado a la base de datos";
        case "sin_conexion": return "Sin conexión: las corridas quedan pendientes y se suben al volver la conexión.";
        case "local": return "Sin cuenta: las corridas quedan pendientes hasta que inicies sesión.";
        case "verificando": return "Verificando la sesión…";
        default: return "La sesión venció: inicia sesión de nuevo para subir las corridas.";
        }
    }

    Connections {
        target: window.cliente
        ignoreUnknownSignals: true
        function onCarpetasCambiadas() { window.corridaSeleccionada = ""; }
    }
    Component.onCompleted: {
        if (cliente) cliente.cargarCatalogo();
        if (historial) historial.recargar();
    }

    // Estado de los monitores en la barra superior: durante una corrida, el que informa el
    // orquestador; fuera de ella, si están instalados/configurados (no si hay conexión)
    function estadoMonitor(nombre) {
        if (!cliente) return { texto: "Sin backend", color: "#8b949e" };
        var med = cliente.medidoresCorrida || [];
        for (var i = 0; i < med.length; i++) {
            if (med[i].nombre !== nombre || !cliente.activa) continue;
            switch (med[i].estado) {
            case "lanzado": return { texto: "Midiendo", color: "#22c55e" };
            case "pendiente": return { texto: "Preparando", color: "#3b82f6" };
            case "termino_antes": return { texto: "Detenido", color: "#f59e0b" };
            default: return { texto: "Cerrando", color: "#3b82f6" };
            }
        }
        var disp = cliente.disponibilidadMonitores();
        if (nombre === "sistema")
            return disp.sistema ? { texto: "Disponible", color: "#3b82f6" } : { texto: "No instalado", color: "#f59e0b" };
        return disp.energia ? { texto: "Configurado", color: "#3b82f6" } : { texto: "Falta entorno", color: "#f59e0b" };
    }
    readonly property var estadoSistema: { var x = cliente ? [cliente.estado, cliente.medidoresCorrida] : []; return estadoMonitor("sistema"); }
    readonly property var estadoEnergia: { var x = cliente ? [cliente.estado, cliente.medidoresCorrida] : []; return estadoMonitor("energia"); }
    Binding { target: window.appState; property: "softwareMonitorStatus"; value: window.estadoSistema.texto }
    Binding { target: window.appState; property: "softwareMonitorColor"; value: window.estadoSistema.color }
    Binding { target: window.appState; property: "energyMonitorStatus"; value: window.estadoEnergia.texto }
    Binding { target: window.appState; property: "energyMonitorColor"; value: window.estadoEnergia.color }
    Binding { target: window.appState; property: "corridaActiva"; value: window.cliente !== null && window.cliente.activa }

    // Al terminar una corrida, el historial se actualiza
    Connections {
        target: window.cliente
        ignoreUnknownSignals: true
        function onEstadoCambiado() {
            var e = window.cliente.estado;
            if (window.historial && (e === "completada" || e === "cancelada" || e === "error"))
                window.historial.recargar();
        }
    }

    Connections {
        target: window.cliente
        ignoreUnknownSignals: true
        function onNotificacion(tipo, titulo, descripcion) {
            window.appState.agregarNotificacion(tipo, titulo, descripcion);
        }
    }
    Connections {
        target: window.subidas
        ignoreUnknownSignals: true
        function onNotificacion(tipo, titulo, descripcion) {
            window.appState.agregarNotificacion(tipo, titulo, descripcion);
        }
        // Modo de subida (D-012): la corrida que termino espera decision; las acciones van en la
        // misma notificacion
        function onDecisionPendiente(dir, nombre, veredicto) {
            window.appState.agregarNotificacion(veredicto === "no_valida" ? "error" : "warning", "La corrida espera tu decisión",
                                                nombre + ": " + window.textoVeredicto(veredicto) + ". " + window.motivosAuditoria(),
                                                dir);
        }
        // Una decision tomada en otra pagina resuelve la notificacion
        function onCambiado() {
            for (var i = 0; i < window.appState.notifications.count; i++) {
                var n = window.appState.notifications.get(i);
                if (n.dir && !n.resuelta && window.subidas.estadoDe(n.dir).estado !== "esperando_decision")
                    window.appState.decisionTomada(n.dir);
            }
        }
    }
    // Aviso del veredicto al terminar (cuando no hay decision pendiente: esa notificacion ya lo dice)
    Connections {
        target: window.cliente
        ignoreUnknownSignals: true
        function onCorridaTerminada(dir) {
            var a = window.cliente.corrida.auditoria;
            if (!a) return;
            if (window.subidas && window.subidas.estadoDe(dir).estado === "esperando_decision") return;
            var nombre = (window.cliente.corrida.experimento || "") + " / corrida " + (window.cliente.corrida.corrida || "");
            window.appState.agregarNotificacion(a.veredicto === "valida" ? "success" : a.veredicto === "no_valida" ? "error" : "warning",
                                                "Auditoría: " + window.textoVeredicto(a.veredicto),
                                                nombre + (a.veredicto === "valida" ? "" : ". " + window.motivosAuditoria()));
        }
    }
    Connections {
        target: window.appState
        function onDecisionSolicitada(dir, accion) {
            if (!window.subidas) return;
            if (accion === "subir_con_nota") {
                dialogoNotaDecision.dirCorrida = dir;
                dialogoNotaDecision.texto = window.historial ? window.historial.nota(dir) : "";
                dialogoNotaDecision.open();
                return;
            }
            window.subidas.decidir(dir, accion === "subir");
        }
    }
    DialogoNota {
        id: dialogoNotaDecision
        property string dirCorrida: ""
        textoAceptar: "Subir con esta nota"
        onNotaAceptada: (texto) => {
            if (window.historial) window.historial.guardarNota(dirCorrida, texto);
            window.subidas.decidir(dirCorrida, true);
        }
    }

    function textoVeredicto(v) {
        return v === "valida" ? "válida" : v === "con_advertencias" ? "con advertencias" : v === "no_valida" ? "no válida" : "sin auditoría";
    }
    // Motivos de las comprobaciones que no dieron ok en la auditoria de la ultima corrida
    function motivosAuditoria() {
        var a = window.cliente && window.cliente.corrida ? window.cliente.corrida.auditoria : null;
        if (!a || !a.comprobaciones) return "";
        return a.comprobaciones.filter(function (c) { return c.resultado === "advertencia" || c.resultado === "no_valida"; })
                               .map(function (c) { return c.motivo; }).join("; ");
    }

    DialogoCuenta { id: dialogoCuenta; objectName: "dialogoCuenta"; sesion: window.sesion }

    readonly property var pageInfo: ({
        "dashboard": { title: "Dashboard", subtitle: "Centro de control de mediciones energéticas" },
        "nuevo_experimento": { title: "Nuevo experimento", subtitle: "Elige el tipo de modelo, sus hiperparámetros y los datos" },
        "experimentos": { title: "Experimento en ejecución", subtitle: "Progreso de la corrida según el proceso de ejecución" },
        "resultados": { title: "Resultados del experimento", subtitle: "Consulta y analiza las métricas obtenidas durante el entrenamiento" },
        "base_datos": { title: "Base de datos", subtitle: "Corridas guardadas en la nube: visibilidad y descarga de la serie unida" },
        "corrida_remota": { title: "Corrida de la base de datos", subtitle: "Resultados calculados desde la serie unida guardada en la nube" },
        "comparar": { title: "Comparar corridas", subtitle: "Corridas de la base de datos lado a lado" },
        "configuracion": { title: "Configuración", subtitle: "Gestiona la configuración de la aplicación y los monitores" }
    })

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.preferredWidth: 238
            Layout.fillHeight: true
            currentPage: window.currentPage
            onPageRequested: (page) => window.currentPage = page
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Barra superior
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                color: "#0d1117"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 16

                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Layout.minimumWidth: 220
                        Text {
                            text: window.pageInfo[window.currentPage] ? window.pageInfo[window.currentPage].title : ""
                            color: "#e6edf3"; font.pixelSize: Tipografia.tituloPrincipal; font.bold: true
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                        Text {
                            text: window.pageInfo[window.currentPage] ? window.pageInfo[window.currentPage].subtitle : ""
                            color: "#8b949e"; font.pixelSize: Tipografia.destacado
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StatusPill {
                        compact: window.width < 1400
                        icon: "monitor"; label: "Monitor de software"; hint: "Sistema operativo · CPU y RAM"
                        status: window.appState.softwareMonitorStatus
                        dotColor: window.appState.softwareMonitorColor
                    }
                    StatusPill {
                        compact: window.width < 1400
                        icon: "rayo"; label: "Monitor físico de energía"; hint: "Medidor Atorch · energía"
                        status: window.appState.energyMonitorStatus
                        dotColor: window.appState.energyMonitorColor
                    }

                    Item { Layout.preferredWidth: 4 }

                    NotificationBell {
                        id: bell
                        unreadCount: window.appState.unreadCount
                        active: notificationCenter.opened
                        onClicked: notificationCenter.opened ? notificationCenter.close() : notificationCenter.open()

                        NotificationCenter {
                            id: notificationCenter
                            appState: window.appState
                            x: bell.width - width
                            y: bell.height + 10
                        }
                    }

                    // Usuario de la base de datos (D-011)
                    Rectangle {
                        id: avatar
                        objectName: "avatarUsuario"
                        width: 32; height: 32; radius: 16
                        color: !window.sesion ? "#22c55e" : window.sesion.conectado ? "#22c55e" : "#f59e0b"
                        Text {
                            anchors.centerIn: parent
                            text: window.inicialesUsuario
                            color: "#0d1117"
                            font.pixelSize: Tipografia.destacado
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: window.sesion !== null
                            cursorShape: Qt.PointingHandCursor
                            onClicked: menuUsuario.opened ? menuUsuario.close() : menuUsuario.open()
                        }

                        Popup {
                            id: menuUsuario
                            objectName: "menuUsuario"
                            x: avatar.width - width
                            y: avatar.height + 10
                            width: 280
                            padding: 14
                            background: Rectangle { color: "#161b22"; border.color: "#30363d"; radius: 10 }
                            contentItem: ColumnLayout {
                                spacing: 8
                                Text {
                                    text: window.sesion && window.sesion.nombre !== "" ? window.sesion.nombre : "Sin cuenta"
                                    color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                                Text {
                                    visible: text !== ""
                                    text: window.sesion ? window.sesion.correo : ""
                                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                                Text {
                                    objectName: "estadoSesion"
                                    text: window.textoEstadoSesion
                                    color: window.sesion && window.sesion.conectado ? "#3fb950" : "#f59e0b"
                                    font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Text {
                                    objectName: "esperandoDecision"
                                    visible: window.subidas !== null && window.subidas.esperandoDecision > 0
                                    text: window.subidas ? window.subidas.esperandoDecision + " corrida(s) esperando tu decisión (Base de datos)" : ""
                                    color: "#f59e0b"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Text {
                                    objectName: "pendientesSubida"
                                    visible: window.subidas !== null && (window.subidas.pendientes > 0 || window.subidas.rechazadas > 0)
                                    text: window.subidas ? (window.subidas.pendientes + " corrida(s) pendiente(s) de subir"
                                                            + (window.subidas.rechazadas > 0 ? ", " + window.subidas.rechazadas + " rechazada(s)" : ""))
                                                         : ""
                                    color: "#f59e0b"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Button {
                                    visible: window.subidas !== null && (window.subidas.pendientes > 0 || window.subidas.rechazadas > 0)
                                    enabled: window.subidas !== null && window.sesion !== null && window.sesion.conectado && !window.subidas.subiendo
                                             && !window.appState.corridaActiva
                                    text: "Reintentar ahora"
                                    Layout.fillWidth: true
                                    onClicked: window.subidas.reintentarTodo()
                                }
                                Text {
                                    visible: window.appState.corridaActiva && window.subidas !== null
                                             && (window.subidas.pendientes > 0 || window.subidas.rechazadas > 0)
                                    text: window.appState.avisoCorridaActiva
                                    color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Button {
                                    objectName: "botonMiCuenta"
                                    visible: window.sesion !== null && window.sesion.estado !== "local"
                                    enabled: window.sesion !== null && window.sesion.conectado
                                    text: "Mi cuenta…"
                                    Layout.fillWidth: true
                                    onClicked: { menuUsuario.close(); dialogoCuenta.open(); }
                                }
                                Button {
                                    objectName: "botonCerrarSesion"
                                    Layout.fillWidth: true
                                    text: window.sesion && window.sesion.estado === "local" ? "Iniciar sesión" : "Cerrar sesión"
                                    // Una corrida en curso sigue en el orquestador, pero se evita
                                    // cambiar de usuario a mitad de una medición
                                    enabled: !(window.cliente && window.cliente.activa)
                                    onClicked: { menuUsuario.close(); window.sesion.cerrarSesion(); }
                                }
                                Text {
                                    visible: window.cliente !== null && window.cliente.activa
                                    text: "Disponible cuando termine la corrida en curso."
                                    color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: {
                    switch (window.currentPage) {
                        case "nuevo_experimento": return nuevoExperimentoPage;
                        case "experimentos": return experimentoEnEjecucionPage;
                        case "resultados": return resultadosPage;
                        case "base_datos": return baseDatosPage;
                        case "corrida_remota": return corridaRemotaPage;
                        case "comparar": return compararPage;
                        case "configuracion": return configuracionPage;
                        default: return dashboardPage;
                    }
                }
            }
        }
    }

    Component {
        id: dashboardPage
        Dashboard {
            appState: window.appState
            historial: window.historial
            subidas: window.subidas
            onNuevoExperimentoSolicitado: window.currentPage = "nuevo_experimento"
            onCorridaSeleccionada: (dir) => { window.corridaSeleccionada = dir; window.currentPage = "resultados"; }
        }
    }
    Component {
        id: nuevoExperimentoPage
        NuevoExperimento {
            appState: window.appState
            cliente: window.cliente
            onExperimentoIniciado: window.currentPage = "experimentos"
        }
    }
    Component {
        id: experimentoEnEjecucionPage
        ExperimentoEnEjecucion {
            appState: window.appState
            cliente: window.cliente
            subidas: window.subidas
            historial: window.historial
            onNuevoExperimentoSolicitado: window.currentPage = "nuevo_experimento"
        }
    }
    Component {
        id: resultadosPage
        Resultados {
            appState: window.appState
            historial: window.historial
            subidas: window.subidas
            remoto: window.remoto
            sesion: window.sesion
            dirCorrida: window.corridaSeleccionada
            onNuevoExperimentoSolicitado: window.currentPage = "nuevo_experimento"
        }
    }
    Component {
        id: baseDatosPage
        BaseDatos {
            appState: window.appState; remoto: window.remoto; sesion: window.sesion; subidas: window.subidas
            onVerCorrida: (id) => { window.remoto.cargarDetalle(id); window.currentPage = "corrida_remota"; }
            onCompararSolicitado: (ids) => { window.remoto.cargarComparacion(ids); window.currentPage = "comparar"; }
        }
    }
    Component {
        id: corridaRemotaPage
        Resultados {
            appState: window.appState
            remoto: window.remoto
            historial: window.historial
            subidas: window.subidas
            sesion: window.sesion
            modoRemoto: true
            onVolverSolicitado: window.currentPage = "base_datos"
            onNuevoExperimentoSolicitado: window.currentPage = "nuevo_experimento"
        }
    }
    Component {
        id: compararPage
        Comparar {
            appState: window.appState
            remoto: window.remoto
            onVolverSolicitado: window.currentPage = "base_datos"
            onVerCorrida: (id) => { window.remoto.cargarDetalle(id); window.currentPage = "corrida_remota"; }
        }
    }
    Component { id: configuracionPage; Configuracion { appState: window.appState; cliente: window.cliente; subidas: window.subidas } }
}
