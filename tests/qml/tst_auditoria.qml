import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// D-012: veredicto y detalle de la auditoría, nota de condiciones, modo de subida y decisiones
// (corridas que esperan decisión). Con dobles de prueba y DATOS INVENTADOS.
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
    Component { id: compEstado; AppState {} }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    Component { id: compDashboard; Dashboard { anchors.fill: parent } }
    Component { id: compBaseDatos; BaseDatos { anchors.fill: parent } }
    Component { id: compConfiguracion; Configuracion { anchors.fill: parent } }
    Component { id: compEjecucion; ExperimentoEnEjecucion { anchors.fill: parent } }
    Component { id: compEstadoBase; EstadoBase {} }
    Component { id: compNotificacion; NotificationItem { width: 360 } }
    Component { id: spyComp; SignalSpy {} }

    function contieneTexto(item, patron) {
        if (item.text !== undefined && String(item.text).indexOf(patron) >= 0 && item.visible) return true;
        var hijos = item.children || [];
        for (var i = 0; i < hijos.length; i++)
            if (contieneTexto(hijos[i], patron)) return true;
        if (item.contentItem && item.contentItem !== item && contieneTexto(item.contentItem, patron)) return true;
        return false;
    }

    TestCase {
        name: "Auditoria"
        when: windowShown

        function sesionActiva() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            return s;
        }

        function test_resultados_muestra_el_veredicto_y_cada_comprobacion() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            compare(findChild(r, "veredictoResultado").value, "Con advertencias");
            verify(findChild(r, "veredictoResultado").sub.indexOf("reglas v1") >= 0, "versión de las reglas");
            compare(findChild(r, "comprobacionesAuditoria").count, 3, "una fila por comprobación");
            verify(raiz.contieneTexto(r, "entrenamiento: 10 de 66 minutos"), "motivo de la comprobación");
            verify(raiz.contieneTexto(r, "15.20 % de los minutos"), "valor con su unidad");
            verify(raiz.contieneTexto(r, "ok <= 10.0 %"), "umbral aplicado");
            verify(raiz.contieneTexto(r, "umbrales provisionales"));
        }

        // Un valor largo ("15.20 % de los minutos", "0 medidores con problemas") se ajusta en varias
        // líneas dentro de su columna y no se monta sobre la columna RESULTADO
        function test_columnas_de_la_auditoria_no_se_superponen() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            wait(20);
            var ids = ["estado", "actividad_ajena", "caidas_ble"];
            for (var i = 0; i < ids.length; i++) {
                var valor = findChild(r, "valorAuditoria_" + ids[i]);
                var resultado = findChild(r, "resultadoAuditoria_" + ids[i]);
                verify(valor !== null && resultado !== null, ids[i]);
                verify(valor.contentWidth <= valor.width + 0.5, ids[i] + ": el valor cabe en su columna");
                verify(resultado.contentWidth <= resultado.width + 0.5, ids[i] + ": el resultado cabe en su columna");
                var finValor = valor.mapToItem(r.contentItem, valor.width, 0).x;
                var inicioResultado = resultado.mapToItem(r.contentItem, 0, 0).x;
                verify(finValor <= inicioResultado, ids[i] + ": el valor termina antes del resultado");
            }
            verify(findChild(r, "valorAuditoria_actividad_ajena").lineCount > 1, "el valor largo se ajusta en varias líneas");
        }

        function test_resultados_sin_auditoria() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, dirCorrida: h.corridas[1].dir });
            compare(findChild(r, "veredictoResultado").value, "Sin auditoría");
            verify(raiz.contieneTexto(r, "no tiene auditoría"));
        }

        function test_nota_local_se_guarda_en_la_carpeta_y_en_la_base() {
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            var rem = createTemporaryObject(compRemoto, raiz);
            var s = sesionActiva();
            sub.elementos = [{ dir: h.corridas[0].dir, estado: "subida", id_remoto: 1, propietario: "propia", origen: "cola" }];
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, subidas: sub, remoto: rem, sesion: s });
            verify(findChild(r, "botonEditarNota").visible);
            r.guardarNota("Ventilador al máximo");
            compare(h.notasGuardadas.length, 1, "nota.txt");
            compare(h.notasGuardadas[0].dir, h.corridas[0].dir);
            compare(rem.notasPedidas.length, 1, "PATCH corrida.nota de la corrida subida y propia");
            compare(rem.notasPedidas[0].id, 1);
            verify(findChild(r, "mensajeNota").text.indexOf("nota.txt y en la base") >= 0);

            // Si la base no responde, la nota queda en nota.txt y se ofrece reintentar
            rem.errorNota = "Sin conexión con la base de datos";
            r.guardarNota("Otra nota");
            verify(findChild(r, "mensajeNota").text.indexOf("La nota no se actualizó en la base") === 0);
            verify(findChild(r, "botonReintentarNota").visible);
            rem.errorNota = "";
            findChild(r, "botonReintentarNota").clicked();
            compare(rem.notasPedidas.length, 3, "reintento");
            compare(findChild(r, "botonReintentarNota").visible, false);
        }

        function test_nota_local_de_una_corrida_no_subida_solo_en_la_carpeta() {
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            var rem = createTemporaryObject(compRemoto, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, subidas: sub, remoto: rem });
            r.guardarNota("Solo local");
            compare(h.notasGuardadas.length, 1);
            compare(rem.notasPedidas.length, 0, "no está en la base: solo nota.txt");
            verify(findChild(r, "mensajeNota").text.indexOf("se sube con la corrida") >= 0);
        }

        function test_nota_local_de_otra_cuenta_no_se_edita() {
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            sub.elementos = [{ dir: h.corridas[0].dir, estado: "pendiente", propietario: "otra", origen: "cola" }];
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, subidas: sub });
            compare(findChild(r, "botonEditarNota").visible, false, "corrida de otra cuenta de este equipo: la nota se ve pero no se edita");
            verify(raiz.contieneTexto(r, "Solo el dueño"));
        }

        function test_nota_remota_solo_la_edita_el_dueno() {
            var rem = createTemporaryObject(compRemoto, raiz);
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            var s = sesionActiva();
            rem.entregarDetalle(2);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, remoto: rem, historial: h, subidas: sub,
                                                                  sesion: s, modoRemoto: true });
            compare(findChild(r, "botonEditarNota").visible, false, "corrida ajena: la nota se ve pero no se edita");
            verify(raiz.contieneTexto(r, "Solo el dueño"));
            rem.entregarDetalle(1);
            verify(findChild(r, "botonEditarNota").visible);
            compare(findChild(r, "textoNota").text, "Sala a 24 grados");
            r.guardarNota("Sala a 25 grados");
            compare(rem.notasPedidas[0].id, 1);
            compare(h.notasGuardadas.length, 0, "la corrida no está en este equipo: solo la columna de la base");
        }

        function test_dashboard_y_base_de_datos_muestran_y_filtran_el_veredicto() {
            var h = createTemporaryObject(compHistorial, raiz);
            var d = createTemporaryObject(compDashboard, raiz, { appState: estadoComun, historial: h });
            compare(findChild(d, "veredicto_0").veredicto, "con_advertencias");
            compare(findChild(d, "veredicto_1").info.texto, "Sin auditoría");

            var rem = createTemporaryObject(compRemoto, raiz);
            var b = createTemporaryObject(compBaseDatos, raiz, { appState: estadoComun, remoto: rem, sesion: sesionActiva() });
            compare(b.corridas.length, 2);
            compare(findChild(b, "veredicto_1").info.texto, "Válida");
            b.filtroVeredicto = "valida";
            compare(b.corridas.length, 1);
            compare(b.corridas[0].id, 1);
            b.filtroVeredicto = "sin";
            compare(b.corridas.length, 1);
            compare(b.corridas[0].id, 2);
            b.filtroVeredicto = "no_valida";
            compare(b.corridas.length, 0);
            verify(raiz.contieneTexto(b, "Ninguna corrida con ese veredicto"));
        }

        function test_base_de_datos_decide_las_que_esperan() {
            var sub = createTemporaryObject(compSubidas, raiz);
            sub.elementos = [{ dir: "C:\\c\\corrida_007", estado: "esperando_decision", experimento: "exp", corrida: 7,
                               propietario: "propia", veredicto: "con_advertencias" }];
            var b = createTemporaryObject(compBaseDatos, raiz, { appState: estadoComun, remoto: createTemporaryObject(compRemoto, raiz),
                                                                 sesion: sesionActiva(), subidas: sub });
            verify(raiz.contieneTexto(b, "Esperando tu decisión"));
            findChild(b, "decidirSubir_7").clicked();
            compare(sub.decisiones.length, 1);
            compare(sub.decisiones[0].subir, true);
        }

        function test_configuracion_cambia_el_modo_de_subida() {
            var sub = createTemporaryObject(compSubidas, raiz);
            var c = createTemporaryObject(compConfiguracion, raiz, { appState: estadoComun, cliente: createTemporaryObject(compCliente, raiz),
                                                                     subidas: sub });
            var combo = findChild(c, "modoSubida");
            compare(combo.currentIndex, 0, "subir todo por defecto");
            combo.activated(1);
            compare(sub.modoSubida, "preguntar_si_hay_problemas");
            combo.activated(2);
            compare(sub.modoSubida, "preguntar_siempre");
        }

        function test_estado_base_esperando_decision() {
            var e = createTemporaryObject(compEstadoBase, raiz, { estado: { estado: "esperando_decision", propietario: "propia" } });
            compare(e.info.texto, "Esperando decisión");
        }

        function test_notificacion_con_acciones_de_decision() {
            var st = createTemporaryObject(compEstado, raiz);
            var spy = createTemporaryObject(spyComp, raiz, { target: st, signalName: "decisionSolicitada" });
            st.agregarNotificacion("warning", "La corrida espera tu decisión", "exp / corrida 7", "C:\\c\\corrida_007");
            compare(st.notifications.get(0).dir, "C:\\c\\corrida_007");
            var n = createTemporaryObject(compNotificacion, raiz, { decision: true });
            verify(findChild(n, "accionesDecision").visible);
            st.decidir(0, "no_subir");
            compare(spy.count, 1);
            compare(spy.signalArguments[0][1], "no_subir");
            compare(st.notifications.get(0).resuelta, true);
        }

        function test_main_resuelve_la_decision_de_la_notificacion() {
            var sub = createTemporaryObject(compSubidas, raiz);
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                            historial: createTemporaryObject(compHistorial, raiz), sesion: sesionActiva(),
                                                            subidas: sub, remoto: createTemporaryObject(compRemoto, raiz) });
            var st = w.appState;
            sub.elementos = [{ dir: "C:\\c\\corrida_007", estado: "esperando_decision", propietario: "propia" }];
            sub.decisionPendiente("C:\\c\\corrida_007", "exp / corrida 7", "con_advertencias");
            compare(st.notifications.count, 1);
            verify(st.notifications.get(0).description.indexOf("con advertencias") >= 0);
            st.decidir(0, "subir");
            compare(sub.decisiones.length, 1);
            compare(sub.decisiones[0].subir, true);
        }

        function test_experimento_en_ejecucion_muestra_la_auditoria_y_las_acciones() {
            var c = createTemporaryObject(compCliente, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            c.estado = "completada";
            c.corrida = { experimento: "exp", corrida: 7, dir_corrida: "C:\\c\\corrida_007", auditoria: {
                veredicto: "con_advertencias", version_reglas: 1,
                comprobaciones: [{ id: "actividad_ajena", resultado: "advertencia", motivo: "entrenamiento: 10 de 66 minutos (15.2 %)" }] } };
            sub.elementos = [{ dir: "C:\\c\\corrida_007", estado: "esperando_decision", propietario: "propia" }];
            var e = createTemporaryObject(compEjecucion, raiz, { appState: estadoComun, cliente: c, subidas: sub,
                                                                 historial: createTemporaryObject(compHistorial, raiz) });
            verify(findChild(e, "tarjetaAuditoria").visible);
            compare(findChild(e, "veredictoCorrida").veredicto, "con_advertencias");
            verify(raiz.contieneTexto(e, "10 de 66 minutos"));
            verify(findChild(e, "accionesDecisionCorrida").visible, "el modo de subida pide decisión");
            findChild(e, "botonDecisionNoSubir").clicked();
            compare(sub.decisiones[0].subir, false);
            compare(findChild(e, "accionesDecisionCorrida").visible, false, "ya decidida");
        }
    }
}
