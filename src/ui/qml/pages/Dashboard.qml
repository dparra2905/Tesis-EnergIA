import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/Ayudas.js" as Ayudas
import "../components/Tipografia.js" as Tipografia

// Panel principal: últimas corridas guardadas (Historial, C++) y resumen de la más reciente.
Flickable {
    id: root
    contentWidth: width
    contentHeight: content.height + 40
    clip: true

    property AppState appState
    // Historial (C++); null si la vista se abre sin backend
    property var historial: null
    // ColaSubidas (C++, D-011); null sin backend: sin columna de base de datos
    property var subidas: null

    signal nuevoExperimentoSolicitado()
    signal corridaSeleccionada(string dir)

    readonly property var corridas: historial ? historial.corridas : []
    readonly property var ultima: corridas.length > 0 ? corridas[0] : null
    readonly property var detalleUltima: historial && ultima ? historial.detalle(ultima.dir) : null

    function fecha(iso) {
        if (!iso) return "—";
        var d = new Date(iso);
        if (isNaN(d.getTime())) return "—";
        function dd(n) { return n < 10 ? "0" + n : "" + n; }
        return dd(d.getDate()) + "/" + dd(d.getMonth() + 1) + "/" + d.getFullYear() + " " + dd(d.getHours()) + ":" + dd(d.getMinutes());
    }
    function hace(iso) {
        var d = new Date(iso);
        if (!iso || isNaN(d.getTime())) return "—";
        var s = Math.max(0, (Date.now() - d.getTime()) / 1000);
        if (s < 60) return "Hace menos de 1 min";
        if (s < 3600) return "Hace " + Math.floor(s / 60) + " min";
        if (s < 86400) return "Hace " + Math.floor(s / 3600) + " h";
        return "Hace " + Math.floor(s / 86400) + " días";
    }
    function duracion(s) {
        if (s === null || s === undefined) return "—";
        return s < 60 ? Number(s).toFixed(1) + " s" : Math.floor(s / 60) + " min " + Math.round(s % 60) + " s";
    }
    function energia(c) {
        return c.energia_experimento && c.energia_experimento.entrenamiento
               ? Number(c.energia_experimento.entrenamiento.energia_wh).toFixed(4) + " Wh" : "—";
    }
    function colorEstado(e) {
        return e === "completada" ? "#22c55e" : (e === "cancelada" ? "#f59e0b" : (e === "en_curso" ? "#3b82f6" : "#f87171"));
    }
    function media(grupo, serie) {
        var v = detalleUltima && detalleUltima.ventanas && detalleUltima.ventanas.entrenamiento
                ? detalleUltima.ventanas.entrenamiento[grupo] : null;
        return v && v[serie] && v[serie].n > 0 ? v[serie].media : null;
    }

    ColumnLayout {
        id: content
        x: 24
        y: 20
        width: root.width - 48
        spacing: 20

        // Tarjeta de bienvenida
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: welcomeCol.height + 40
            radius: 12
            color: "#161b22"
            border.color: "#21262d"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 20

                ColumnLayout {
                    id: welcomeCol
                    Layout.fillWidth: true
                    spacing: 10

                    Text { text: "Bienvenido a EnergIA"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloPagina; font.bold: true }
                    Text {
                        text: "Mide el consumo de recursos y de energía durante el entrenamiento de modelos de Machine Learning."
                        color: "#8b949e"; font.pixelSize: Tipografia.destacado; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 10
                        Button {
                            text: "+  Nuevo experimento"
                            implicitHeight: 42
                            background: Rectangle { radius: 8; color: "#22c55e" }
                            contentItem: Text { text: parent.text; color: "#0d1117"; font.pixelSize: Tipografia.destacado; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: root.nuevoExperimentoSolicitado()
                        }
                        Button {
                            text: "Ver resultados"
                            enabled: root.ultima !== null
                            implicitHeight: 42
                            onClicked: root.corridaSeleccionada(root.ultima.dir)
                        }
                    }
                }
            }
        }

        // Tarjetas de estadísticas
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            StatCard {
                objectName: "statTotal"
                title: "Corridas guardadas"
                value: root.historial ? String(root.corridas.length) : "—"
                caption: root.historial ? root.historial.dirSalida : "Sin backend"
                Layout.fillWidth: true
            }
            StatCard {
                objectName: "statUltima"
                title: "Última corrida"
                value: root.ultima ? root.hace(root.ultima.t_creacion) : "—"
                caption: root.ultima ? root.ultima.modelo_nombre + " · " + root.ultima.estado : "Ninguna todavía"
                Layout.fillWidth: true
            }
            StatCard {
                title: "Completadas"
                value: root.historial ? String(root.corridas.filter(function (c) { return c.estado === "completada"; }).length) : "—"
                caption: "de " + root.corridas.length
                Layout.fillWidth: true
            }
            StatCard {
                title: "Con energía medida"
                value: root.historial ? String(root.corridas.filter(function (c) { return c.energia_experimento !== undefined; }).length) : "—"
                caption: "monitor físico de energía"
                Layout.fillWidth: true
            }
        }

        // Las tablas ocupan todo el ancho en ventanas medianas; con espacio suficiente,
        // el resumen vuelve al lateral. Así las etiquetas grandes no se superponen.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 1520 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            // Últimas corridas
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.alignment: Qt.AlignTop
                implicitHeight: tableCol.height + 32
                radius: 12
                color: "#161b22"
                border.color: "#21262d"

                ColumnLayout {
                    id: tableCol
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 16
                    spacing: 12

                    Text { text: "Últimas corridas"; color: "#e6edf3"; font.pixelSize: Tipografia.tituloSeccion; font.bold: true }

                    Text {
                        visible: root.corridas.length === 0
                        text: root.historial ? "Todavía no hay corridas guardadas." : "Vista sin backend: no hay corridas."
                        color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                    }

                    RowLayout {
                        visible: root.corridas.length > 0
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "TIPO DE MODELO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 130 }
                        Text { text: "CORRIDA"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 140 }
                        Text { text: "FECHA"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 100 }
                        Text { text: "ENTRENAMIENTO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 80 }
                        Text { text: "ENERGÍA"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 80 }
                        Text { text: "ESTADO"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true }
                        Text { text: "AUDITORÍA"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 100 }
                        Text { visible: root.subidas !== null; text: "BASE DE DATOS"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 90 }
                    }

                    Repeater {
                        objectName: "tablaCorridas"
                        model: root.corridas.slice(0, 8)
                        delegate: ColumnLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Text { text: modelData.modelo_nombre; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; font.bold: true; Layout.preferredWidth: 130; elide: Text.ElideRight }
                                Text { text: modelData.id; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 140; elide: Text.ElideRight }
                                Text { text: root.fecha(modelData.t_start || modelData.t_creacion); color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 100 }
                                Text { text: root.duracion(modelData.duracion_entrenamiento_s); color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 80 }
                                Text { text: root.energia(modelData); color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 80 }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: root.colorEstado(modelData.estado) }
                                    Text { text: modelData.estado; color: root.colorEstado(modelData.estado); font.pixelSize: Tipografia.cuerpo }
                                    Item { Layout.fillWidth: true }
                                    Veredicto {
                                        objectName: "veredicto_" + index
                                        Layout.preferredWidth: 100
                                        veredicto: modelData.veredicto || ""
                                    }
                                    EstadoBase {
                                        objectName: "estadoBase_" + index
                                        Layout.preferredWidth: 90
                                        estado: { var x = root.subidas ? root.subidas.elementos : []; return root.subidas ? root.subidas.estadoDe(modelData.dir) : null; }
                                    }
                                    Text {
                                        text: "Ver"; color: "#3b82f6"; font.pixelSize: Tipografia.cuerpo
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.corridaSeleccionada(modelData.dir) }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Resumen de la última corrida: grupos separados (D-002).
            // Se colocan lado a lado cuando la tabla se presenta a ancho completo.
            GridLayout {
                columns: root.width >= 1520 ? 1 : 2
                Layout.preferredWidth: root.width >= 1520 ? 300 : root.width - 48
                Layout.fillWidth: root.width < 1520
                Layout.alignment: Qt.AlignTop
                columnSpacing: 16
                rowSpacing: 16

                SectionCard {
                    Layout.fillWidth: true
                    title: "Uso de recursos computacionales"
                    subtitle: "Última corrida · promedio durante el entrenamiento · monitor de software"
                    accent: root.appState.resourcesAccent
                    ayuda: Ayudas.texto("grupo_recursos")

                    Text {
                        visible: root.media("sistema", "cpu_proceso_pct") === null
                        text: "Sin datos del monitor de software."
                        color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                    }
                    ResourceBar {
                        visible: root.media("sistema", "cpu_proceso_pct") !== null
                        label: "CPU del proceso de ejecución"
                        percent: Math.round(root.media("sistema", "cpu_proceso_pct") || 0)
                        barColor: "#3b82f6"; Layout.fillWidth: true
                    }
                    ResourceBar {
                        visible: root.media("sistema", "ram_sistema_pct") !== null
                        label: "RAM del sistema"
                        percent: Math.round(root.media("sistema", "ram_sistema_pct") || 0)
                        barColor: "#a78bfa"; Layout.fillWidth: true
                    }
                }

                SectionCard {
                    Layout.fillWidth: true
                    title: "Métricas de energía"
                    subtitle: "Última corrida · durante el entrenamiento · monitor físico de energía"
                    accent: root.appState.energyAccent
                    ayuda: Ayudas.texto("grupo_energia")

                    Text {
                        visible: root.media("energia", "potencia_w") === null
                        text: "Sin datos del monitor físico de energía."
                        color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                    }
                    GridLayout {
                        visible: root.media("energia", "potencia_w") !== null
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10

                        LiveMetricCard {
                            Layout.fillWidth: true
                            icon: "enchufe"; label: "Potencia media"; tint: "#f59e0b"; unit: "W"
                            ayuda: Ayudas.texto("potencia_w")
                            value: root.media("energia", "potencia_w") !== null ? Number(root.media("energia", "potencia_w")).toFixed(1) : "—"
                        }
                        LiveMetricCard {
                            Layout.fillWidth: true
                            icon: "rayo"; label: "Energía del experimento"; tint: "#22c55e"; unit: "Wh"
                            ayuda: Ayudas.texto("energia")
                            value: root.ultima && root.ultima.energia_experimento && root.ultima.energia_experimento.entrenamiento
                                   ? Number(root.ultima.energia_experimento.entrenamiento.energia_wh).toFixed(4) : "—"
                        }
                    }
                }
            }
        }
    }
}
