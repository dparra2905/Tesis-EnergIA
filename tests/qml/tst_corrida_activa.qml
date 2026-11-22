import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// Regla de corrida activa: mientras una corrida está activa no se sube a la base ni se exporta
// (botones deshabilitados con el aviso); leer y mostrar sigue permitido. Con dobles de prueba.
Item {
    id: raiz
    width: 1300
    height: 1000

    AppState { id: estadoComun }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compSubidas; SubidasFalsa {} }
    Component { id: compRemoto; RemotoFalso {} }
    Component { id: compSesion; SesionFalsa {} }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compMain; Main {} }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: compBaseDatos; BaseDatos { anchors.fill: parent } }
    Component { id: compNotificacion; NotificationItem { width: 360 } }

    function contieneTexto(item, patron) {
        if (item.text !== undefined && String(item.text).indexOf(patron) >= 0 && item.visible) return true;
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++)
            if (contieneTexto(hijos[i], patron)) return true;
        if (item.contentItem && item.contentItem !== item && contieneTexto(item.contentItem, patron)) return true;
        return false;
    }

    TestCase {
        name: "CorridaActiva"
        when: windowShown

        function cleanup() { estadoComun.corridaActiva = false; }

        function sesionActiva() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            return s;
        }

        function test_main_enlaza_la_regla_al_cliente() {
            var c = createTemporaryObject(compCliente, raiz);
            var w = createTemporaryObject(compMain, raiz, { cliente: c, historial: createTemporaryObject(compHistorial, raiz),
                                                            sesion: sesionActiva(), subidas: createTemporaryObject(compSubidas, raiz),
                                                            remoto: createTemporaryObject(compRemoto, raiz) });
            compare(w.appState.corridaActiva, false);
            c.activa = true;
            compare(w.appState.corridaActiva, true, "corridaActiva sigue a cliente.activa");
            c.activa = false;
            compare(w.appState.corridaActiva, false);
        }

        function test_resultados_no_sube_ni_exporta_durante_una_corrida() {
            var st = estadoComun;
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: st, historial: h, subidas: sub });
            var subir = findChild(r, "botonSubir"), exportar = findChild(r, "botonExportar");
            verify(subir.visible && subir.enabled, "sin corrida activa se puede subir");
            verify(exportar.enabled, "sin corrida activa se puede exportar el CSV");
            verify(!findChild(r, "avisoCorridaActiva").visible);
            st.corridaActiva = true;
            verify(!subir.enabled, "Subir a la base deshabilitado");
            verify(!exportar.enabled, "Exportar CSV deshabilitado");
            verify(raiz.contieneTexto(r, "Disponible cuando termine la corrida en curso"));
            verify(raiz.contieneTexto(r, "Auditoría de la corrida"), "el detalle se sigue mostrando");
            st.corridaActiva = false;
            verify(subir.enabled && exportar.enabled, "al terminar la corrida vuelven a estar disponibles");
        }

        function test_base_de_datos_no_sube_ni_exporta_durante_una_corrida() {
            var st = estadoComun;
            var sub = createTemporaryObject(compSubidas, raiz);
            sub.elementos = [{ dir: "C:\\c\\corrida_007", estado: "esperando_decision", experimento: "exp", corrida: 7,
                               propietario: "propia", veredicto: "con_advertencias" }];
            var b = createTemporaryObject(compBaseDatos, raiz, { appState: st, remoto: createTemporaryObject(compRemoto, raiz),
                                                                 sesion: sesionActiva(), subidas: sub });
            st.corridaActiva = true;
            verify(!findChild(b, "decidirSubir_7").enabled, "Subir deshabilitado");
            verify(findChild(b, "decidirNoSubir_7").enabled, "No subir no sube nada: sigue disponible");
            verify(findChild(b, "avisoCorridaActivaPendientes").visible);
        }

        function test_notificacion_espera_para_subir() {
            var n = createTemporaryObject(compNotificacion, raiz, { decision: true, corridaActiva: true,
                                                                   avisoCorridaActiva: "Disponible cuando termine la corrida en curso" });
            verify(!findChild(n, "accion_subir").enabled);
            verify(!findChild(n, "accion_subir_con_nota").enabled);
            verify(findChild(n, "accion_no_subir").enabled);
            verify(raiz.contieneTexto(n, "Disponible cuando termine la corrida en curso"));
        }
    }
}
