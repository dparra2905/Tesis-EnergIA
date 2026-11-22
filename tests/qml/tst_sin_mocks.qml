import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// F12: la interfaz no presenta datos de demostración como reales.
Item {
    id: raiz
    width: 1300
    height: 900

    Component { id: compEstado; AppState {} }
    AppState { id: estadoComun }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compMain; Main {} }
    Component { id: compConfiguracion; Configuracion { anchors.fill: parent } }

    function contieneTexto(item, patron) {
        if (item.text !== undefined && String(item.text).indexOf(patron) >= 0 && item.visible) return true;
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++)
            if (contieneTexto(hijos[i], patron)) return true;
        return false;
    }

    TestCase {
        name: "SinMocks"
        when: windowShown

        function test_sin_notificaciones_de_demostracion() {
            var e = createTemporaryObject(compEstado, raiz);
            compare(e.notifications.count, 0);
            compare(e.unreadCount, 0);
            compare(e.energyMonitorEnabled, false, "el monitor físico no se incluye por defecto");
            compare(e.energyMonitorStatus, "Sin verificar", "nunca 'Conectado' sin verificarlo");
        }

        function test_estado_de_los_monitores_en_la_barra() {
            var c = createTemporaryObject(compCliente, raiz);
            var w = createTemporaryObject(compMain, raiz, { cliente: c, historial: createTemporaryObject(compHistorial, raiz) });
            verify(w !== null);
            compare(w.appState.softwareMonitorStatus, "Disponible");
            compare(w.appState.energyMonitorStatus, "Configurado");
            c.systemMeterInstalado = false;
            c.laboratorio = { mac: "", python: "x", python_existe: false, script: "y", script_existe: true };
            c.estado = "inactivo";  // fuerza la reevaluación
            compare(w.appState.softwareMonitorStatus, "No instalado");
            compare(w.appState.energyMonitorStatus, "Falta entorno");
            c.activa = true;
            c.estado = "en_curso";
            c.medidoresCorrida = [{ nombre: "sistema", estado: "lanzado" }];
            compare(w.appState.softwareMonitorStatus, "Midiendo");
            w.close();
        }

        function test_configuracion_con_datos_reales() {
            var c = createTemporaryObject(compCliente, raiz);
            var p = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: c });
            compare(findChild(p, "filaCpu").valor, "CPU de prueba");
            compare(findChild(p, "carpetaResultados").ruta, c.dirSalida);
            verify(!raiz.contieneTexto(p, "i7-12700H") && !raiz.contieneTexto(p, "Guardar cambios")
                   && !raiz.contieneTexto(p, "Límite de registros"), "sin preferencias ni datos simulados");
        }
    }
}
