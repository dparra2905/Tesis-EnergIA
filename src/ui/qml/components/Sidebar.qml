import QtQuick
import QtQuick.Layouts
import "Tipografia.js" as Tipografia

Rectangle {
    id: root
    property string currentPage: "dashboard"
    signal pageRequested(string page)

    color: "#0b0f14"
    border.color: "#21262d"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 24

        RowLayout {
            objectName: "marcaEnergIA"
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 44; Layout.preferredHeight: 44
                radius: 10; color: "#22c55e"
                Icono { anchors.centerIn: parent; nombre: "rayo"; tamano: 26; grosor: 2; color: "#0d1117" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { text: "EnergIA"; color: "#e6edf3"; font.pixelSize: Tipografia.marca + 6; font.bold: true }
                Text { text: "MONITOR"; color: "#22c55e"; font.pixelSize: Tipografia.auxiliar }
            }
        }

        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            SidebarItem { label: "Dashboard"; icon: "panel"; active: root.currentPage === "dashboard"; Layout.fillWidth: true; onClicked: root.pageRequested("dashboard") }
            SidebarItem { label: "Nuevo experimento"; icon: "nuevo"; active: root.currentPage === "nuevo_experimento"; Layout.fillWidth: true; onClicked: root.pageRequested("nuevo_experimento") }
            SidebarItem { label: "Experimentos"; icon: "matraz"; active: root.currentPage === "experimentos"; Layout.fillWidth: true; onClicked: root.pageRequested("experimentos") }
            SidebarItem { label: "Resultados"; icon: "barras"; active: root.currentPage === "resultados"; Layout.fillWidth: true; onClicked: root.pageRequested("resultados") }
            SidebarItem { label: "Base de datos"; icon: "base_datos"; active: root.currentPage === "base_datos" || root.currentPage === "corrida_remota" || root.currentPage === "comparar"; Layout.fillWidth: true; onClicked: root.pageRequested("base_datos") }
            SidebarItem { label: "Configuración"; icon: "ajustes"; active: root.currentPage === "configuracion"; Layout.fillWidth: true; onClicked: root.pageRequested("configuracion") }
        }

        Item { Layout.fillHeight: true }
    }
}
