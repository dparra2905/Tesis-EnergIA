import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Tipografia.js" as Tipografia
import "Ayudas.js" as Ayudas

// Desempeño del modelo en Resultados: una sola seccion con la evaluacion que entrega la DLL y las
// metricas de clasificacion de metricas_modelo.json (D-015), para no mostrar la exactitud dos veces.
// 'evaluacion' es la lista de metricas de evaluar() que arma Resultados ({clave, titulo, valor,
// detalle}): la exactitud en validacion es la misma que la de las metricas del modelo, asi que su
// valor original (val_accuracy = ...) va como pie de la tarjeta de exactitud; las demas (p. ej.
// val_loss de la MLP) van como tarjetas extra en Validacion. Si la corrida no tiene metricas del
// modelo (anterior a ellas o sin entrenamiento completo), se muestran solo las de 'evaluacion'.
// 'metricas' es el archivo (detalle local) o su copia de la base (modo remoto, corrida.metricas_modelo):
// {} si la corrida no tiene metricas (vieja o sin entrenamiento completo): "No disponible".
// Selector Validacion / Entrenamiento; por conjunto: exactitud y promedios macro y ponderado, matriz de
// confusion (filas = clase real, columnas = clase predicha; color por la fraccion de la fila, con el
// conteo y el porcentaje por fila) y tabla por clase con la marca de las metricas indefinidas
// (division por cero: valen 0, como scikit-learn con zero_division=0). Si predecir fallo en el
// conjunto, en lugar de la matriz: "No se pudieron calcular las metricas: <motivo>".
SectionCard {
    id: root
    objectName: "calidadModelo"
    title: "Desempeño del modelo"
    subtitle: disponible
              ? "Métricas de clasificación calculadas con las predicciones del modelo, después de la medición (no entran en la energía ni en los tiempos)."
              : evaluacion.length > 0 ? "Evaluación en validación al terminar el entrenamiento, calculada por la DLL del modelo." : ""
    accent: "#a78bfa"
    ayuda: Ayudas.texto("modelo")

    property var metricas: ({})
    property var evaluacion: []
    // "validacion" | "entrenamiento"
    property string conjunto: "validacion"

    readonly property bool disponible: metricas !== null && metricas !== undefined
                                       && metricas.validacion !== undefined && metricas.entrenamiento !== undefined
    readonly property var datos: disponible ? metricas[conjunto] : null
    readonly property bool calculado: datos !== null && datos !== undefined && datos.status_dll === 0
                                      && datos.matriz_confusion !== null && datos.matriz_confusion !== undefined
    readonly property var clases: disponible && metricas.clases_originales ? metricas.clases_originales : []
    readonly property var matriz: calculado ? datos.matriz_confusion : []
    // La entrada de evaluar() con esta clave, o null
    function deEvaluacion(clave) {
        for (var i = 0; i < evaluacion.length; i++) if (evaluacion[i].clave === clave) return evaluacion[i];
        return null;
    }
    // Metricas de evaluar() que no repiten la exactitud: tarjetas extra en Validacion
    readonly property var extrasValidacion: {
        var l = [];
        for (var i = 0; i < evaluacion.length; i++) if (evaluacion[i].clave !== "val_accuracy") l.push(evaluacion[i]);
        return l;
    }
    readonly property bool hayIndefinidas: {
        if (!calculado) return false;
        for (var i = 0; i < datos.por_clase.length; i++) {
            var c = datos.por_clase[i];
            if (c.precision.indefinida || c.recall.indefinida || c.f1.indefinida) return true;
        }
        return false;
    }

    function num(v, dec) { return v === null || v === undefined || isNaN(v) ? "—" : Number(v).toFixed(dec); }
    function pct(v) { return v === null || v === undefined || isNaN(v) ? "—" : (100 * v).toFixed(1) + " %"; }
    function etiqueta(i) { return i < clases.length ? String(clases[i]) : String(i); }
    // Metrica por clase: el valor y un asterisco si no esta definida
    function valorClase(m) { return m ? num(m.valor, 3) + (m.indefinida ? " *" : "") : "—"; }
    function totalFila(fila) { var t = 0; for (var j = 0; j < fila.length; j++) t += fila[j]; return t; }
    function colorCelda(fraccion, diagonal) {
        // Intensidad por la fraccion de la fila: verde en la diagonal (aciertos), rojo fuera (errores)
        var a = 0.08 + 0.72 * Math.max(0, Math.min(1, fraccion));
        return diagonal ? Qt.rgba(0.13, 0.77, 0.37, a) : Qt.rgba(0.97, 0.32, 0.29, fraccion > 0 ? a : 0.04);
    }

    // Sin metricas del modelo: la evaluacion de la DLL, si la hay
    RowLayout {
        objectName: "evaluacionSola"
        visible: !root.disponible && root.evaluacion.length > 0
        Layout.fillWidth: true
        spacing: 14
        Repeater {
            model: root.disponible ? [] : root.evaluacion
            delegate: StatCard {
                required property var modelData
                objectName: "statModelo_" + modelData.clave
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                title: modelData.titulo; value: modelData.valor; caption: modelData.detalle
                ayuda: Ayudas.texto(modelData.clave)
            }
        }
        Item { Layout.fillWidth: true }
    }
    Text {
        objectName: "calidadNoDisponible"
        visible: !root.disponible
        text: root.evaluacion.length > 0
              ? "Sin matriz de confusión ni métricas por clase: esta corrida es anterior a ellas o su entrenamiento no terminó."
              : "No disponible: esta corrida no tiene métricas del modelo (no se ejecutó la evaluación en validación, es anterior a ellas o su entrenamiento no terminó)."
        color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }

    // Selector Validacion / Entrenamiento
    RowLayout {
        objectName: "selectorConjunto"
        visible: root.disponible
        spacing: 6
        Repeater {
            model: [{ clave: "validacion", texto: "Validación" }, { clave: "entrenamiento", texto: "Entrenamiento" }]
            delegate: Rectangle {
                required property var modelData
                objectName: "conjunto_" + modelData.clave
                readonly property bool elegido: root.conjunto === modelData.clave
                implicitWidth: textoConjunto.implicitWidth + 24
                implicitHeight: 26
                radius: 6
                color: elegido ? "#1f6feb" : "#21262d"
                border.color: elegido ? "#388bfd" : "#30363d"
                Text {
                    id: textoConjunto
                    anchors.centerIn: parent
                    text: modelData.texto + (root.disponible && root.metricas[modelData.clave] && root.metricas[modelData.clave].n_filas !== undefined
                                             ? " (" + root.metricas[modelData.clave].n_filas + " filas)" : "")
                    color: elegido ? "#ffffff" : "#c9d1d9"; font.pixelSize: Tipografia.cuerpo
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.conjunto = modelData.clave }
            }
        }
    }

    Text {
        objectName: "calidadError"
        visible: root.disponible && !root.calculado
        text: "No se pudieron calcular las métricas: " + (root.datos && root.datos.motivo ? root.datos.motivo : "sin datos")
        color: "#f85149"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }

    // Exactitud y promedios
    GridLayout {
        objectName: "resumenCalidad"
        visible: root.calculado
        Layout.fillWidth: true
        columns: root.width >= 760 ? 3 : root.width >= 440 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12
        StatCard {
            objectName: "statExactitud"
            readonly property var original: root.conjunto === "validacion" ? root.deEvaluacion("val_accuracy") : null
            title: root.conjunto === "validacion" ? "Exactitud en validación" : "Exactitud en entrenamiento"
            dotColor: "#a78bfa"; Layout.fillWidth: true
            value: root.calculado ? root.pct(root.datos.exactitud) : "—"
            caption: original ? original.detalle : ""
            ayuda: Ayudas.texto("val_accuracy")
        }
        StatCard {
            objectName: "statPromedioMacro"
            title: "Promedio macro"; Layout.fillWidth: true
            value: root.calculado ? "F1 " + root.num(root.datos.macro.f1, 3) : "—"
            caption: root.calculado ? "precisión " + root.num(root.datos.macro.precision, 3) + " · recall " + root.num(root.datos.macro.recall, 3)
                                      + " · media simple de las clases" : ""
        }
        StatCard {
            objectName: "statPromedioPonderado"
            title: "Promedio ponderado"; Layout.fillWidth: true
            value: root.calculado ? "F1 " + root.num(root.datos.ponderado.f1, 3) : "—"
            caption: root.calculado ? "precisión " + root.num(root.datos.ponderado.precision, 3) + " · recall " + root.num(root.datos.ponderado.recall, 3)
                                      + " · pesada por el soporte" : ""
        }
        // Otras metricas de evaluar() (p. ej. la perdida en validacion): solo en Validacion
        Repeater {
            model: root.conjunto === "validacion" ? root.extrasValidacion : []
            delegate: StatCard {
                required property var modelData
                objectName: "statModelo_" + modelData.clave
                Layout.fillWidth: true
                title: modelData.titulo; value: modelData.valor; caption: modelData.detalle
                ayuda: Ayudas.texto(modelData.clave)
            }
        }
    }

    // Matriz de confusion
    Text {
        visible: root.calculado
        text: "Matriz de confusión: filas = clase real, columnas = clase predicha. Cada celda: conteo y porcentaje de su fila."
        color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }
    ScrollView {
        visible: root.calculado
        Layout.fillWidth: true
        implicitHeight: Math.min(tablaMatriz.implicitHeight + 16, 420)
        contentWidth: tablaMatriz.implicitWidth
        clip: true
        Grid {
            id: tablaMatriz
            objectName: "matrizConfusion"
            columns: root.matriz.length + 1
            spacing: 2
            // Encabezado: esquina y clases predichas
            Text { width: 110; height: 24; text: "real \\ predicha"; color: "#484f58"; font.pixelSize: Tipografia.detalle; verticalAlignment: Text.AlignVCenter }
            Repeater {
                model: root.matriz.length
                delegate: Text {
                    required property int index
                    width: 64; height: 22
                    text: root.etiqueta(index); color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                }
            }
            // Filas: etiqueta real y celdas (n x n, en orden de filas)
            Repeater {
                model: root.matriz.length * (root.matriz.length + 1)
                delegate: Item {
                    required property int index
                    readonly property int fila: Math.floor(index / (root.matriz.length + 1))
                    readonly property int col: index % (root.matriz.length + 1) - 1
                    readonly property var valores: root.matriz[fila]
                    readonly property int total: root.totalFila(valores)
                    readonly property int conteo: col >= 0 ? valores[col] : 0
                    width: col < 0 ? 90 : 64
                    height: 36
                    Text {
                        visible: col < 0
                        anchors.fill: parent
                        text: root.etiqueta(fila); color: "#8b949e"; font.pixelSize: Tipografia.auxiliar; font.bold: true
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: col >= 0
                        anchors.fill: parent
                        radius: 4
                        color: root.colorCelda(total > 0 ? conteo / total : 0, fila === col)
                        Column {
                            anchors.centerIn: parent
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: conteo; color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; font.bold: true }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: total > 0 ? (100 * conteo / total).toFixed(1) + " %" : "—"
                                color: "#c9d1d9"; font.pixelSize: Tipografia.detalle
                            }
                        }
                    }
                }
            }
        }
    }

    // Tabla por clase
    ColumnLayout {
        objectName: "tablaPorClase"
        visible: root.calculado
        Layout.fillWidth: true
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Text { text: "CLASE"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.preferredWidth: 120 }
            Text { text: "PRECISIÓN"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70 }
            Text { text: "RECALL"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70 }
            Text { text: "F1"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70 }
            Text { text: "SOPORTE"; color: "#484f58"; font.pixelSize: Tipografia.detalle; Layout.fillWidth: true; Layout.preferredWidth: 70 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#21262d" }
        Repeater {
            model: root.calculado ? root.datos.por_clase : []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Text { text: String(modelData.etiqueta); color: "#e6edf3"; font.pixelSize: Tipografia.cuerpo; Layout.preferredWidth: 120; elide: Text.ElideRight }
                Text { text: root.valorClase(modelData.precision); color: modelData.precision.indefinida ? "#d29922" : "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70 }
                Text { text: root.valorClase(modelData.recall); color: modelData.recall.indefinida ? "#d29922" : "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70 }
                Text { text: root.valorClase(modelData.f1); color: modelData.f1.indefinida ? "#d29922" : "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70 }
                Text { text: String(modelData.soporte); color: "#c9d1d9"; font.pixelSize: Tipografia.cuerpo; Layout.fillWidth: true; Layout.preferredWidth: 70 }
            }
        }
        Text {
            objectName: "notaIndefinidas"
            visible: root.hayIndefinidas
            text: "* Indefinida (división por cero): la clase nunca se predijo (precisión) o no tiene filas (recall); vale 0, como scikit-learn con zero_division=0."
            color: "#d29922"; font.pixelSize: Tipografia.auxiliar; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }
}
