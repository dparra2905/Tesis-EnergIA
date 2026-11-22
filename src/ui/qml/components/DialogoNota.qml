import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

// Nota de condiciones de una corrida (D-012): texto libre opcional (p. ej. "ventilador al máximo",
// "navegador abierto"). Se guarda en nota.txt de la corrida y, si está en la base y es del
// usuario con sesión, también en la base. Quien abre el diálogo decide qué hacer con el texto.
Dialog {
    id: root
    objectName: "dialogoNota"
    property string texto: ""
    // Texto del botón de aceptar ("Guardar", "Subir con esta nota"...)
    property string textoAceptar: "Guardar"
    signal notaAceptada(string texto)

    title: "Nota de condiciones"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 440

    onOpened: { campo.text = root.texto; campo.forceActiveFocus(); }

    contentItem: ColumnLayout {
        spacing: 8
        Text {
            text: "Condiciones en que se midió la corrida (carga del equipo, temperatura de la sala, otros programas…). "
                  + "Es opcional y se puede cambiar después."
            color: "#8b949e"; font.pixelSize: Tipografia.cuerpo; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            TextArea {
                id: campo
                objectName: "campoNota"
                wrapMode: TextArea.Wrap
                placeholderText: "Sin nota"
                selectByMouse: true
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "botonAceptarNota"
            text: root.textoAceptar
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button { text: "Cancelar"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    onAccepted: root.notaAceptada(campo.text)
}
