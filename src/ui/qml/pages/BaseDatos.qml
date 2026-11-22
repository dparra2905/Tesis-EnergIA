import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../components/Tipografia.js" as Tipografia

// Corridas de la base de datos (D-011): las propias, las compartidas y, para un admin, todas.
// Cada dueño decide si su corrida es privada o compartida. Desde aquí solo se descarga la
// serie unida (rejilla de 1 s); los CSV crudos de los medidores se quedan en el PC que midió.
Flickable {
    id: root
    contentWidth: width
    contentHeight: columna.height + 40
    clip: true

    property AppState appState
    // HistorialRemoto (C++); null sin backend
    property var remoto: null
    // ClienteSupabase (C++); null sin backend
    property var sesion: null
    // ColaSubidas (C++); null sin backend
    property var subidas: null

    // "todas" | "mias" | "compartidas"
    property string filtro: "todas"
    // Filtro por veredicto de la auditoria (D-012): "" (todos) | valida | con_advertencias |
    // no_valida | sin (corridas sin auditoria)
    property string filtroVeredicto: ""
    property string mensaje: ""
    property real idExportar: 0
    // Regla de corrida activa (AppState): no se sube ni se exporta mientras una corrida mide
    readonly property bool corridaActiva: appState !== null && appState.corridaActiva
    // Corridas elegidas para comparar (ids, hasta 4)
    property var seleccion: []
    readonly property int maxComparar: 4

    signal verCorrida(real id)
    signal compararSolicitado(var ids)

    function alternar(id) {
        var i = seleccion.indexOf(id);
        if (i >= 0) seleccion = seleccion.filter(function (x) { return x !== id; });
        else if (seleccion.length < maxComparar) seleccion = seleccion.concat([id]);
    }

    readonly property bool conectado: sesion !== null && sesion.conectado
    readonly property var corridas: {
        var l = remoto ? remoto.corridas : [];
        if (filtro === "mias") l = l.filter(function (c) { return c.propia; });
        else if (filtro === "compartidas") l = l.filter(function (c) { return !c.propia; });
        if (filtroVeredicto === "sin") l = l.filter(function (c) { return !c.veredicto; });
        else if (filtroVeredicto !== "") l = l.filter(function (c) { return c.veredicto === filtroVeredicto; });
        return l;
    }
    readonly property var noSubidas: subidas ? subidas.elementos.filter(function (e) { return e.estado !== "subida"; }) : []

    function num(v, dec) { return v === null || v === undefined ? "—" : Number(v).toFixed(dec); }
    function fecha(iso) {
        if (!iso) return "—";
        var d = new Date(iso);
        if (isNaN(d.getTime())) return iso;
        function dd(n) { return n < 10 ? "0" + n : "" + n; }
        return dd(d.getDate()) + "/" + dd(d.getMonth() + 1) + "/" + d.getFullYear() + " " + dd(d.getHours()) + ":" + dd(d.getMinutes());
    }
    function textoEstadoCola(e) {
        if (e.propietario === "otra") return "De otra cuenta de este equipo: la sube su dueño";
        if (e.propietario === "sin_cuenta") return "Terminada sin cuenta: no se sube sola";
        switch (e.estado) {
        case "esperando_decision": return "Esperando tu decisión (modo de subida)";
        case "pendiente": return "Pendiente" + (e.ultimo_error ? ": " + e.ultimo_error : "");
        case "subiendo": return "Subiendo…";
        case "rechazada": return "Rechazada: " + e.ultimo_error;
        default: return e.estado;
        }
    }

    // Se carga al abrir la página y cada vez que vuelve la conexión
    property bool iniciada: false
    Component.onCompleted: { iniciada = true; if (remoto && conectado) remoto.recargar(); }
    onConectadoChanged: if (iniciada && remoto && conectado && remoto.estado !== "cargando") remoto.recargar()

    Connections {
        target: root.remoto
        ignoreUnknownSignals: true
        function onVisibilidadCambiada(id, visibilidad, error) {
            if (error !== "") { root.mensaje = error; root.remoto.recargar(); }  // el interruptor vuelve a su estado real
            else root.mensaje = "La corrida " + id + " ahora es " + visibilidad + ".";
        }
        function onExportacionTerminada(id, destino, filas, error) {
            root.mensaje = error !== "" ? error : "Exportado (" + filas + " filas): " + destino;
        }
    }
    // Al subir una corrida nueva, la lista se actualiza
    Connections {
        target: root.subidas
        ignoreUnknownSignals: true
        function onCorridaSubida(dir, id) { if (root.remoto && root.conectado) root.remoto.recargar(); }
    }

    FileDialog {
        id: dialogoExportar
        title: "Exportar la serie unida (rejilla de 1 s) desde la base de datos"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: root.remoto.exportarCsv(root.idExportar, root.remoto.rutaLocal(selectedFile))
    }

    ColumnLayout {
        id: columna
        x: 24
        y: 20
        width: root.width - 48
        spacing: 16

        // Sin sesión
        SectionCard {
            Layout.fillWidth: true
            visible: !root.conectado
            title: !root.sesion ? "Vista sin backend" : "Sin conexión con la base de datos"
            subtitle: !root.sesion ? "No hay sesión: no hay corridas remotas que mostrar."
                    : root.sesion.estado === "local" ? "Estás usando EnergIA sin cuenta. Inicia sesión desde el menú de usuario para ver y subir corridas."
                    : root.sesion.estado === "sin_conexion" ? "No hay conexión. Las corridas siguen guardándose en este equipo y se suben cuando vuelva la conexión."
                    : "La sesión venció. Inicia sesión de nuevo desde el menú de usuario."
            accent: "#f59e0b"
        }

        // Pendientes de este equipo
        SectionCard {
            objectName: "tarjetaPendientes"
            Layout.fillWidth: true
            visible: root.noSubidas.length > 0
            title: "Pendientes de subir desde este equipo (" + root.noSubidas.length + ")"
            subtitle: "Las tuyas se suben solas al haber sesión y conexión. Las terminadas sin cuenta solo se suben si las reclamas."
            accent: "#f59e0b"

            Button {
                objectName: "botonReclamar"
                visible: root.subidas !== null && root.subidas.sinCuenta > 0
                text: "Subir las " + (root.subidas ? root.subidas.sinCuenta : 0) + " corrida(s) sin cuenta a mi cuenta"
                enabled: root.conectado && !root.corridaActiva
                onClicked: root.subidas.reclamarSinCuenta()
            }
            Text {
                objectName: "avisoCorridaActivaPendientes"
                visible: root.corridaActiva
                text: root.appState ? root.appState.avisoCorridaActiva : ""
                color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
            }

            Repeater {
                model: root.noSubidas
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: (modelData.experimento || "") + " / corrida " + (modelData.corrida || "?")
                        color: "#e6edf3"; font.pixelSize: Tipografia.destacado; Layout.preferredWidth: 220; elide: Text.ElideRight
                    }
                    Text {
                        text: root.textoEstadoCola(modelData)
                        color: modelData.estado === "rechazada" ? "#f85149" : "#8b949e"
                        font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; elide: Text.ElideRight
                    }
                    Veredicto { veredicto: modelData.veredicto || "" }
                    // Modo de subida (D-012): la corrida espera decision
                    Button {
                        objectName: "decidirSubir_" + (modelData.corrida || "")
                        visible: modelData.estado === "esperando_decision" && modelData.propietario !== "otra"
                        text: "Subir"
                        enabled: !root.corridaActiva
                        onClicked: root.subidas.decidir(modelData.dir, true)
                    }
                    Button {
                        objectName: "decidirNoSubir_" + (modelData.corrida || "")
                        visible: modelData.estado === "esperando_decision" && modelData.propietario !== "otra"
                        text: "No subir"
                        onClicked: root.subidas.decidir(modelData.dir, false)
                    }
                    Button {
                        visible: modelData.propietario !== "otra" && modelData.estado !== "esperando_decision"
                        text: modelData.propietario === "sin_cuenta" ? "Subir a mi cuenta" : "Reintentar"
                        enabled: root.conectado && !root.subidas.subiendo && !root.corridaActiva
                        onClicked: root.subidas.reintentar(modelData.dir)
                    }
                }
            }
        }

        // Corridas en la base
        SectionCard {
            objectName: "tarjetaCorridasRemotas"
            Layout.fillWidth: true
            visible: root.conectado
            title: "Corridas en la base de datos"
            subtitle: root.sesion && root.sesion.esAdmin
                      ? "Como administrador ves todas las corridas. Solo el dueño de cada una puede cambiar su visibilidad."
                      : "Tus corridas (privadas por defecto) y las que otros usuarios compartieron."
            accent: "#3b82f6"

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [{ clave: "todas", texto: "Todas" }, { clave: "mias", texto: "Mías" }, { clave: "compartidas", texto: "De otros" }]
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "filtro_" + modelData.clave
                        width: etiqueta.implicitWidth + 24; height: 28; radius: 14
                        color: root.filtro === modelData.clave ? "#1f6feb33" : "transparent"
                        border.color: root.filtro === modelData.clave ? "#3b82f6" : "#30363d"
                        Text {
                            id: etiqueta
                            anchors.centerIn: parent
                            text: modelData.texto
                            color: root.filtro === modelData.clave ? "#e6edf3" : "#8b949e"; font.pixelSize: Tipografia.cuerpo
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.filtro = modelData.clave }
                    }
                }
                ComboBox {
                    id: comboVeredicto
                    objectName: "filtroVeredicto"
                    readonly property var claves: ["", "valida", "con_advertencias", "no_valida", "sin"]
                    model: ["Cualquier veredicto", "Válida", "Con advertencias", "No válida", "Sin auditoría"]
                    implicitWidth: 170
                    onActivated: (i) => root.filtroVeredicto = claves[i]
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.remoto && root.remoto.estado === "cargando" ? "Cargando…"
                        : root.remoto && root.remoto.estado === "error" ? root.remoto.error : ""
                    color: root.remoto && root.remoto.estado === "error" ? "#f85149" : "#8b949e"; font.pixelSize: Tipografia.cuerpo
                }
                BotonIcono {
                    objectName: "botonComparar"
                    icono: "comparar"
                    text: "Comparar (" + root.seleccion.length + ")"
                    enabled: root.seleccion.length >= 2
                    onClicked: root.compararSolicitado(root.seleccion)
                }
                BotonIcono {
                    objectName: "botonRecargar"
                    icono: "recargar"
                    text: "Recargar"
                    enabled: root.remoto !== null && root.remoto.estado !== "cargando"
                    onClicked: root.remoto.recargar()
                }
            }

            Text {
                objectName: "mensajeBaseDatos"
                visible: root.mensaje !== ""
                text: root.mensaje
                color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
            }

            Text {
                visible: root.remoto !== null && root.remoto.estado === "listo" && root.corridas.length === 0
                text: root.filtroVeredicto !== "" ? "Ninguna corrida con ese veredicto."
                    : root.filtro === "compartidas" ? "Nadie ha compartido corridas todavía."
                                                    : "No hay corridas en la base. Las corridas que termines se suben solas."
                color: "#8b949e"; font.pixelSize: Tipografia.destacado
            }

            // Encabezado
            RowLayout {
                visible: root.corridas.length > 0
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.preferredWidth: 28 }
                Text { text: "Corrida"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 200 }
                Text { text: "Modelo"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 120 }
                Text { text: "Equipo · dueño"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.fillWidth: true }
                Text { text: "Energía entr."; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 80 }
                Text { text: "Exactitud"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 60 }
                Text { text: "Auditoría"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 100 }
                Text { text: "Compartida"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; Layout.preferredWidth: 70 }
                Item { Layout.preferredWidth: 170 }
            }

            Repeater {
                objectName: "listaCorridasRemotas"
                model: root.corridas
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: fila.implicitHeight + 16
                    radius: 8
                    color: "#0d1117"
                    border.color: "#21262d"

                    RowLayout {
                        id: fila
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10

                        Casilla {
                            objectName: "comparar_" + modelData.id
                            Layout.preferredWidth: 28
                            checked: root.seleccion.indexOf(modelData.id) >= 0
                            enabled: checked || root.seleccion.length < root.maxComparar
                            onToggled: root.alternar(modelData.id)
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.preferredWidth: 200
                            Text {
                                text: modelData.experimento + " / corrida " + modelData.numero
                                color: "#e6edf3"; font.pixelSize: Tipografia.destacado; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.estado + " · subida " + root.fecha(modelData.subida_en)
                                color: modelData.estado === "completada" ? "#8b949e" : "#f59e0b"; font.pixelSize: Tipografia.auxiliar
                            }
                        }
                        Text {
                            text: String(modelData.modelo_dll || "").replace(/^EnergIA_/, "").replace(/\.dll$/, "")
                            color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 120; elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.equipo + " · " + (modelData.propia ? "tú" : (modelData.dueno || "otro usuario"))
                            color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.energia_entrenamiento_wh === null || modelData.energia_entrenamiento_wh === undefined
                                  ? "—" : root.num(modelData.energia_entrenamiento_wh, 3) + " Wh"
                            color: "#fbbf24"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 80
                        }
                        Text {
                            text: modelData.accuracy_val === null || modelData.accuracy_val === undefined ? "—" : root.num(modelData.accuracy_val * 100, 1) + " %"
                            color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 60
                        }
                        Veredicto {
                            objectName: "veredicto_" + modelData.id
                            Layout.preferredWidth: 100
                            veredicto: modelData.veredicto || ""
                        }
                        Item {
                            Layout.preferredWidth: 70
                            implicitHeight: 22
                            ToggleSwitch {
                                objectName: "visibilidad_" + modelData.id
                                visible: modelData.propia
                                checked: modelData.visibilidad === "compartida"
                                onToggled: (c) => root.remoto.cambiarVisibilidad(modelData.id, c ? "compartida" : "privada")
                            }
                            Text {
                                visible: !modelData.propia
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.visibilidad === "compartida" ? "Sí" : "No (admin)"
                                color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                            }
                        }
                        Button {
                            objectName: "ver_" + modelData.id
                            Layout.preferredWidth: 56
                            text: "Ver"
                            onClicked: root.verCorrida(modelData.id)
                        }
                        BotonIcono {
                            objectName: "exportar_" + modelData.id
                            Layout.preferredWidth: 100
                            icono: root.remoto && root.remoto.exportando === modelData.id ? "" : "descargar"
                            text: root.remoto && root.remoto.exportando === modelData.id ? "Descargando…" : "CSV"
                            enabled: root.remoto !== null && root.remoto.exportando === 0 && !root.corridaActiva
                             ToolTip.visible: hovered && root.corridaActiva
                             ToolTip.text: root.appState ? root.appState.avisoCorridaActiva : ""
                            onClicked: { root.idExportar = modelData.id; dialogoExportar.open(); }
                        }
                    }
                }
            }
        }
    }
}
