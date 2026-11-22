import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../components/Tipografia.js" as Tipografia

// Configuración y datos del equipo. Solo muestra información real: el estado de los monitores,
// los valores por defecto de los nuevos experimentos, las rutas que usa EnergIA, el medidor del
// laboratorio y los datos del equipo. No hay preferencias simuladas.
Flickable {
    id: root
    contentWidth: width
    contentHeight: content.height + 40
    clip: true

    property AppState appState
    // ClienteOrquestador (C++); null si la vista se abre sin backend
    property var cliente: null
    // ColaSubidas (C++): modo de subida a la base de datos (D-012); null sin backend
    property var subidas: null
    readonly property var laboratorio: cliente && cliente.medidorEnergiaLaboratorio ? cliente.medidorEnergiaLaboratorio() : null
    readonly property var sistema: cliente && cliente.infoSistema ? cliente.infoSistema() : null
    property string mensajeCarpetas: ""
    property bool carpetasCorrectas: false

    function elegirCarpeta(tipo, ruta) {
        if (!cliente || cliente.activa) return;
        dialogoCarpeta.tipo = tipo;
        dialogoCarpeta.title = "Selecciona la carpeta de " +
            (tipo === "resultados" ? "resultados" : tipo === "modelos" ? "modelos" : "datasets");
        dialogoCarpeta.currentFolder = cliente.urlCarpeta(ruta);
        dialogoCarpeta.open();
    }

    FolderDialog {
        id: dialogoCarpeta
        objectName: "dialogoSeleccionCarpeta"
        property string tipo: ""
        onAccepted: {
            if (!root.cliente) return;
            var error = root.cliente.configurarCarpeta(tipo, selectedFolder);
            root.carpetasCorrectas = error === "";
            root.mensajeCarpetas = error === ""
                    ? "Carpeta guardada. Se utilizará en los próximos experimentos."
                    : error;
        }
    }

    ColumnLayout {
        id: content
        x: 24
        y: 20
        width: root.width - 48
        spacing: 20

        Text {
            visible: !root.cliente
            text: "Vista sin backend: no hay datos de configuración del equipo."
            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
        }

        // Carpetas editables: la elección se guarda y se aplica sin reiniciar EnergIA.
        Tarjeta {
            objectName: "seccionCarpetas"
            titulo: "1. Carpetas de trabajo"
            Text {
                text: "Entrada: datasets y algoritmos para entrenar. Salida: resultados generados por los experimentos. Cada carpeta es independiente."
                color: "#8b949e"; font.pixelSize: Tipografia.cuerpo
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 1080 ? 3 : 1
                columnSpacing: 12
                rowSpacing: 12

                OpcionCarpeta {
                    objectName: "carpetaDatasets"
                    tipo: "datasets"
                    categoria: "DATOS DE ENTRADA"
                    titulo: "Datasets (CSV)"
                    detalle: "Archivos que se seleccionan para entrenar. Esta es la carpeta inicial del explorador de CSV."
                    ruta: root.cliente ? root.cliente.dirDatasets : ""
                }
                OpcionCarpeta {
                    objectName: "carpetaModelos"
                    tipo: "modelos"
                    categoria: "RECURSOS DE ENTRADA"
                    titulo: "Algoritmos de entrenamiento"
                    detalle: "Aquí se buscan las DLL EnergIA_*.dll utilizadas para ejecutar los modelos."
                    ruta: root.cliente ? root.cliente.dirModelos : ""
                }
                OpcionCarpeta {
                    objectName: "carpetaResultados"
                    tipo: "resultados"
                    categoria: "DATOS DE SALIDA"
                    titulo: "Resultados de experimentos"
                    detalle: "Aquí se guardan las nuevas corridas, las métricas y los archivos de resultados."
                    ruta: root.cliente ? root.cliente.dirSalida : ""
                }
            }
            Text {
                objectName: "mensajeCarpetas"
                visible: root.mensajeCarpetas !== ""
                text: root.mensajeCarpetas
                color: root.carpetasCorrectas ? "#4ade80" : "#f59e0b"
                font.pixelSize: Tipografia.auxiliar
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                text: "Cambiar una carpeta no mueve ni borra corridas anteriores. " +
                      "Si eliges otra carpeta de resultados, el historial mostrará las corridas de la nueva ubicación."
                color: "#8b949e"; font.pixelSize: Tipografia.detalle
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 880 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            // 2. Monitores
            Tarjeta {
                objectName: "tarjetaMonitores"
                Layout.fillHeight: true
                titulo: "2. Monitores"


                MonitorStatusRow {
                    objectName: "filaMonitorSoftware"
                    Layout.fillWidth: true
                    name: "Monitor de software"
                    detail: "CPU y RAM del proceso de ejecución y del sistema (system_meter). Interruptor: incluirlo por defecto en los nuevos experimentos."
                    status: root.appState.softwareMonitorStatus
                    statusColor: root.appState.softwareMonitorColor
                    showToggle: true
                    checked: root.appState.softwareMonitorEnabled
                    onToggled: (checked) => root.appState.softwareMonitorEnabled = checked
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
                MonitorStatusRow {
                    objectName: "filaMonitorEnergia"
                    Layout.fillWidth: true
                    name: "Monitor físico de energía"
                    detail: "Atorch S1BW por Bluetooth (energia_ble.py). Interruptor: incluirlo por defecto en los nuevos experimentos."
                    status: root.appState.energyMonitorStatus
                    statusColor: root.appState.energyMonitorColor
                    showToggle: true
                    checked: root.appState.energyMonitorEnabled
                    onToggled: (checked) => root.appState.energyMonitorEnabled = checked
                }
                Text {
                    text: "Fuera de una corrida se indica si cada monitor está instalado o configurado; la conexión con el Atorch solo se verifica al iniciar una corrida."
                    color: "#484f58"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }

                // Configuración real del medidor de energía (laboratorio.json)
                ColumnLayout {
                    objectName: "laboratorioEnergia"
                    visible: root.laboratorio !== null
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Medidor de energía del laboratorio"; color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                    Text {
                        text: "Atorch S1BW · MAC " + (root.laboratorio && root.laboratorio.mac ? root.laboratorio.mac : "la del script por defecto")
                        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Icono {
                            objectName: "iconoEntornoEnergia"
                            nombre: root.laboratorio && root.laboratorio.python_existe ? "check" : "alerta"
                            tamano: 12
                            color: root.laboratorio && root.laboratorio.python_existe ? "#22c55e" : "#f59e0b"
                            Layout.alignment: Qt.AlignTop
                        }
                        Text {
                            objectName: "estadoEntornoEnergia"
                            text: root.laboratorio
                                  ? (root.laboratorio.python_existe ? "Entorno de Python del medidor: " : "Falta el entorno de Python del medidor (crear_entorno.bat): ")
                                    + root.laboratorio.python
                                  : ""
                            color: root.laboratorio && root.laboratorio.python_existe ? "#22c55e" : "#f59e0b"
                            font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Icono {
                            nombre: root.laboratorio && root.laboratorio.script_existe ? "check" : "alerta"
                            tamano: 12
                            color: root.laboratorio && root.laboratorio.script_existe ? "#8b949e" : "#f59e0b"
                            Layout.alignment: Qt.AlignTop
                        }
                        Text {
                            text: root.laboratorio ? (root.laboratorio.script_existe ? "" : "No se encuentra el script: ") + root.laboratorio.script : ""
                            color: root.laboratorio && root.laboratorio.script_existe ? "#8b949e" : "#f59e0b"
                            font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
                        }
                    }
                    Text { text: "Se edita en laboratorio.json, junto a la aplicación."; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true }
                }
            }

            // 3. Ejecución y datos
            Tarjeta {
                objectName: "tarjetaEjecucion"
                Layout.fillHeight: true
                titulo: "3. Ejecución y datos"

                Campo {
                    etiqueta: "Proceso de ejecución"
                    icono: root.cliente ? (root.cliente.orquestadorEncontrado ? "check" : "alerta") : ""
                    colorIcono: root.cliente && root.cliente.orquestadorEncontrado ? "#22c55e" : "#f59e0b"
                    valor: root.cliente ? (root.cliente.orquestadorEncontrado ? "" : "No encontrado: ") + root.cliente.rutaOrquestador : "—"
                }
                Campo {
                    etiqueta: "Modo"
                    valor: root.cliente ? (root.cliente.modoDesarrollo ? "Desarrollo (DLL de prueba y datos sintéticos visibles)" : "Normal") : "—"
                }
                Campo { etiqueta: "Rejilla temporal común (D-007)"; valor: "1 s, alineada a t_start" }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Línea base por defecto (s)"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar }
                    SpinBox {
                        id: lineaBaseDefecto
                        objectName: "lineaBaseDefecto"
                        enabled: root.cliente !== null
                        from: 0; to: 3600; editable: true
                        value: root.cliente ? root.cliente.lineaBaseSegundos : 20
                        onValueModified: if (root.cliente) root.cliente.lineaBaseSegundos = value
                    }
                    Text {
                        text: "Reposo medido antes y después del entrenamiento en las corridas nuevas (el mismo valor para las dos fases). Se puede cambiar en cada corrida y se conserva entre ejecuciones. La energía de cada fase se calcula sin corregir el entrenamiento (D-008)."
                        color: "#484f58"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Modo de subida a la base de datos"; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar }
                    ComboBox {
                        id: modoSubida
                        objectName: "modoSubida"
                        readonly property var claves: ["subir_todo", "preguntar_si_hay_problemas", "preguntar_siempre"]
                        enabled: root.subidas !== null
                        Layout.fillWidth: true
                        model: ["Subir todo", "Preguntar si hay problemas", "Preguntar siempre"]
                        currentIndex: root.subidas ? Math.max(0, claves.indexOf(root.subidas.modoSubida)) : 0
                        onActivated: (i) => { if (root.subidas) root.subidas.modoSubida = claves[i]; }
                    }
                    Text {
                        text: "Qué hacer al terminar una corrida según el veredicto de su auditoría (D-012). «Subir todo» sube todas, como siempre. "
                              + "«Preguntar si hay problemas» sube sola la válida y deja las demás (también las que no tienen auditoría) "
                              + "esperando tu decisión. «Preguntar siempre» deja todas esperando. Subir a mano desde Resultados no pregunta. "
                              + "Se guarda en preferencias.json."
                        color: "#484f58"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
                Text {
                    text: "Las opciones de inicio --orquestador, --desarrollo y --laboratorio se configuran al abrir EnergIA. " +
                          "Las carpetas se pueden cambiar arriba; las rutas pasadas por línea de comandos tienen prioridad al iniciar."
                    color: "#484f58"; font.pixelSize: Tipografia.detalle; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

        }

        // Resumen técnico ancho completo: no deja un bloque huérfano a la izquierda.
        Tarjeta {
                objectName: "tarjetaEquipo"
                titulo: "4. Información del equipo"
                visible: root.sistema !== null

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 1080 ? 3 : 2
                    columnSpacing: 24
                    rowSpacing: 18
                    Fila { etiqueta: "Sistema operativo"; valor: root.sistema ? root.sistema.sistema_operativo : "" }
                Fila { objectName: "filaCpu"; etiqueta: "CPU"; valor: root.sistema && root.sistema.cpu ? root.sistema.cpu : "—" }
                Fila { etiqueta: "Núcleos lógicos"; valor: root.sistema ? String(root.sistema.nucleos_logicos) : "" }
                Fila {
                    etiqueta: "Memoria total"
                    valor: root.sistema && root.sistema.memoria_total_gb ? Number(root.sistema.memoria_total_gb).toFixed(1) + " GB" : "—"
                }
                Fila { etiqueta: "Arquitectura"; valor: root.sistema ? root.sistema.arquitectura : "" }
                Fila { etiqueta: "Qt"; valor: root.sistema ? root.sistema.version_qt : "" }
                }
            }
    }

    component Tarjeta: Rectangle {
        id: tarjeta
        property string titulo: ""
        default property alias contenido: cuerpo.data
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: col.height + 40
        radius: 12; color: "#161b22"; border.color: "#21262d"
        ColumnLayout {
            id: col
            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
            anchors.margins: 20
            spacing: 12
            Text { text: tarjeta.titulo; color: "#e6edf3"; font.pixelSize: Tipografia.subtitulo; font.bold: true }
            ColumnLayout { id: cuerpo; Layout.fillWidth: true; spacing: 12 }
        }
    }

    component Campo: ColumnLayout {
        id: campo
        property string etiqueta: ""
        property string valor: ""
        // Icono de estado opcional delante del valor (Iconos.js)
        property string icono: ""
        property color colorIcono: "#8b949e"
        Layout.fillWidth: true
        spacing: 4
        Text { text: campo.etiqueta; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Icono { visible: campo.icono !== ""; nombre: campo.icono; tamano: 12; color: campo.colorIcono; Layout.alignment: Qt.AlignTop }
            Text { text: campo.valor; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        }
    }

    component Fila: ColumnLayout {
        property string etiqueta: ""
        property string valor: ""
        Layout.fillWidth: true
        spacing: 5
        Text {
            text: etiqueta; color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
            Layout.fillWidth: true
        }
        Text {
            text: valor; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo
            font.bold: true
            elide: Text.ElideMiddle; Layout.fillWidth: true
            ToolTip.visible: valorMouse.hovered && implicitWidth > width
            ToolTip.text: valor
            HoverHandler { id: valorMouse }
        }
    }

    component OpcionCarpeta: Rectangle {
        id: opcion
        property string tipo: ""
        property string titulo: ""
        property string detalle: ""
        property string categoria: ""
        property string ruta: ""
        Layout.fillWidth: true
        implicitHeight: Math.max(224, fila.implicitHeight + 28)
        radius: 10
        color: "#0d1117"
        border.color: "#30363d"
        ColumnLayout {
            id: fila
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14
            spacing: 9
            Text {
                objectName: "tipoFlujoCarpeta"
                text: opcion.categoria
                color: opcion.tipo === "resultados" ? "#4ade80" : "#60a5fa"
                font.pixelSize: Tipografia.detalle
                font.bold: true
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icono {
                    nombre: "carpeta"; tamano: 17
                    color: opcion.tipo === "resultados" ? "#4ade80" : "#60a5fa"
                }
                Text {
                    text: opcion.titulo; font.pixelSize: Tipografia.cuerpo; font.bold: true
                    color: "#e6edf3"; Layout.fillWidth: true
                }
            }
            Text {
                text: opcion.detalle
                color: "#8b949e"; font.pixelSize: Tipografia.auxiliar
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 35
                radius: 7
                color: "#161b22"
                border.color: "#21262d"
                Text {
                    id: rutaVisible
                    anchors.fill: parent; anchors.margins: 9
                    verticalAlignment: Text.AlignVCenter
                    text: opcion.ruta !== "" ? opcion.ruta : "Sin configurar"
                    color: "#c9d1d9"; font.pixelSize: Tipografia.auxiliar
                    elide: Text.ElideMiddle
                    ToolTip.visible: rutaHover.hovered
                    ToolTip.text: opcion.ruta
                    HoverHandler { id: rutaHover }
                }
            }
            BotonIcono {
                objectName: "examinar_" + opcion.tipo
                icono: "carpeta"
                text: "Examinar carpeta"
                enabled: root.cliente !== null && !root.cliente.activa
                Layout.fillWidth: true
                implicitHeight: 38
                onClicked: root.elegirCarpeta(opcion.tipo, opcion.ruta)
                ToolTip.visible: hovered && !enabled
                ToolTip.text: "No se pueden cambiar las carpetas durante una corrida."
            }
        }
    }
}
