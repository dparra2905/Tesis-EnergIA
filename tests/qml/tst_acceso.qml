import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// D-011: pantalla de inicio de sesión y registro, y menú del usuario en Main.
Item {
    id: raiz
    width: 1200
    height: 800

    Component { id: compSesion; SesionFalsa {} }
    Component { id: compAcceso; Acceso { anchors.fill: parent } }
    Component { id: compCliente; ClienteFalso {} }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compMain; Main {} }
    Component { id: compSubidas; SubidasFalsa {} }
    Component { id: compRemoto; RemotoFalso {} }
    Component { id: compDashboard; Dashboard { anchors.fill: parent } }
    Component { id: compComparar; Comparar { anchors.fill: parent } }
    Component { id: compBaseDatos; BaseDatos { anchors.fill: parent } }
    Component { id: compResultados; Resultados { anchors.fill: parent } }
    AppState { id: estadoComun }

    TestCase {
        name: "Acceso"
        when: windowShown

        function escribir(campo, texto) { campo.text = texto; }

        function test_login() {
            var s = createTemporaryObject(compSesion, raiz);
            var a = createTemporaryObject(compAcceso, raiz, { sesion: s });
            var enviar = findChild(a, "botonEnviar");
            compare(enviar.enabled, false, "sin datos no se puede enviar");
            escribir(findChild(a, "campoCorreo"), "ana@ejemplo.com");
            escribir(findChild(a, "campoContrasena"), "mala");
            compare(enviar.enabled, true);
            mouseClick(enviar);
            compare(findChild(a, "textoError").text, "Correo o contraseña incorrectos.");
            escribir(findChild(a, "campoContrasena"), "secreta1");
            mouseClick(enviar);
            compare(s.estado, "activa");
            compare(s.puedeEntrar, true);
        }

        function test_registro_valida_y_avisa() {
            var s = createTemporaryObject(compSesion, raiz);
            var a = createTemporaryObject(compAcceso, raiz, { sesion: s });
            mouseClick(findChild(a, "pestana_registrar"));
            compare(a.modo, "registrar");
            verify(findChild(a, "campoNombre").visible);
            escribir(findChild(a, "campoNombre"), "Nuevo");
            escribir(findChild(a, "campoCorreo"), "nuevo@ejemplo.com");
            escribir(findChild(a, "campoContrasena"), "123");
            compare(findChild(a, "textoError").text, "La contraseña debe tener al menos 6 caracteres.");
            escribir(findChild(a, "campoContrasena"), "clave-larga");
            escribir(findChild(a, "campoConfirmacion"), "otra");
            compare(findChild(a, "textoError").text, "Las contraseñas no coinciden.");
            compare(findChild(a, "botonEnviar").enabled, false);
            escribir(findChild(a, "campoConfirmacion"), "clave-larga");
            mouseClick(findChild(a, "botonEnviar"));
            compare(s.registros.length, 1);
            compare(s.registros[0].nombre, "Nuevo");
            compare(a.modo, "entrar", "tras registrarse vuelve a Iniciar sesión");
            verify(findChild(a, "textoAviso").text.indexOf("confirmación") >= 0);
            compare(findChild(a, "campoCorreo").text, "nuevo@ejemplo.com", "conserva el correo");
            compare(s.estado, "sin_sesion", "con confirmación de correo no hay sesión todavía");
        }

        function test_sin_red_ofrece_continuar() {
            var s = createTemporaryObject(compSesion, raiz, { sinRed: true });
            var a = createTemporaryObject(compAcceso, raiz, { sesion: s });
            var sinConexion = findChild(a, "botonSinConexion");
            verify(!sinConexion.visible, "sin error de red no se ofrece");
            escribir(findChild(a, "campoCorreo"), "ana@ejemplo.com");
            escribir(findChild(a, "campoContrasena"), "secreta1");
            mouseClick(findChild(a, "botonEnviar"));
            verify(sinConexion.visible);
            mouseClick(sinConexion);
            compare(s.estado, "local");
            compare(s.puedeEntrar, true);
        }

        function test_verificando_oculta_el_formulario() {
            var s = createTemporaryObject(compSesion, raiz, { estado: "verificando" });
            var a = createTemporaryObject(compAcceso, raiz, { sesion: s });
            verify(!findChild(a, "botonEnviar").visible);
        }

        function test_menu_de_usuario_en_main() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var c = createTemporaryObject(compCliente, raiz);
            var w = createTemporaryObject(compMain, raiz, { cliente: c, historial: createTemporaryObject(compHistorial, raiz), sesion: s });
            compare(w.inicialesUsuario, "AP");
            var cerrar = findChild(w, "botonCerrarSesion");
            verify(cerrar !== null);
            c.activa = true;
            compare(cerrar.enabled, false, "no se cierra sesión durante una corrida");
            c.activa = false;
            compare(cerrar.enabled, true);
            cerrar.clicked();
            compare(s.cierres, 1);
            w.close();
        }

        function test_estado_de_subida_en_resultados() {
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, subidas: sub });
            var dir = h.corridas[0].dir;
            compare(findChild(r, "estadoSubida").text, "Todavía no está en la base de datos.");
            var boton = findChild(r, "botonSubir");
            verify(boton.visible);
            boton.clicked();
            compare(sub.reintentos[0], dir, "pide subir la corrida mostrada");
            verify(findChild(r, "estadoSubida").text.indexOf("Pendiente") === 0);
            sub.elementos = [{ dir: dir, estado: "subida", id_remoto: 7, ultimo_error: "" }];
            compare(findChild(r, "estadoSubida").text, "Guardada en la base de datos (id 7).");
            verify(!boton.visible, "ya subida: sin botón");
            sub.elementos = [{ dir: dir, estado: "rechazada", ultimo_error: "invalid input syntax" }];
            verify(boton.visible && boton.text.indexOf("Reintentar") >= 0);
        }

        function test_pendientes_en_el_menu() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var sub = createTemporaryObject(compSubidas, raiz, { elementos: [{ dir: "a", estado: "pendiente" }, { dir: "b", estado: "rechazada" }] });
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                             historial: createTemporaryObject(compHistorial, raiz), sesion: s, subidas: sub });
            var t = findChild(w, "pendientesSubida");
            compare(t.text, "1 corrida(s) pendiente(s) de subir, 1 rechazada(s)");
            sub.notificacion("success", "Corrida guardada en la base de datos", "exp / corrida 1");
            compare(w.appState.notifications.count, 1, "las notificaciones de la cola llegan al centro de notificaciones");
            w.close();
        }

        function test_pagina_base_de_datos() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var rem = createTemporaryObject(compRemoto, raiz);
            var sub = createTemporaryObject(compSubidas, raiz, { elementos: [{ dir: "C:/x", experimento: "exp_a", corrida: 9, estado: "pendiente", ultimo_error: "" }] });
            var p = createTemporaryObject(compBaseDatos, raiz, { appState: estadoComun, remoto: rem, sesion: s, subidas: sub });
            compare(rem.recargas, 1, "al abrir con sesión se carga la lista");
            verify(findChild(p, "tarjetaPendientes").visible, "muestra las pendientes de este equipo");
            compare(p.corridas.length, 2);
            mouseClick(findChild(p, "filtro_mias"));
            compare(p.corridas.length, 1);
            compare(p.corridas[0].id, 1);
            mouseClick(findChild(p, "filtro_todas"));
            wait(20);  // los delegados del filtro anterior se destruyen en diferido
            var sw = findChild(p, "visibilidad_1");
            verify(sw !== null && sw.visible, "la corrida propia tiene interruptor");
            var ajeno = findChild(p, "visibilidad_2");
            verify(ajeno === null || !ajeno.visible, "la ajena no");
            mouseClick(sw);
            compare(rem.cambios.length, 1);
            compare(rem.cambios[0].visibilidad, "compartida");
            compare(findChild(p, "mensajeBaseDatos").text, "La corrida 1 ahora es compartida.");
            rem.exportacionTerminada(2, "C:/tmp/x.csv", 332, "");
            compare(findChild(p, "mensajeBaseDatos").text, "Exportado (332 filas): C:/tmp/x.csv");
            s.estado = "sin_conexion";
            verify(!findChild(p, "tarjetaCorridasRemotas").visible, "sin conexión se oculta la lista remota");
        }

        function test_main_navega_a_base_de_datos() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                             historial: createTemporaryObject(compHistorial, raiz), sesion: s,
                                                             subidas: createTemporaryObject(compSubidas, raiz),
                                                             remoto: createTemporaryObject(compRemoto, raiz) });
            w.currentPage = "base_datos";
            wait(50);
            verify(findChild(w.contentItem, "tarjetaCorridasRemotas") !== null);
            w.close();
        }

        function test_marca_de_base_en_dashboard() {
            var h = createTemporaryObject(compHistorial, raiz);
            var sub = createTemporaryObject(compSubidas, raiz, { elementos: [
                { dir: h.corridas[0].dir, estado: "subida", propietario: "propia", id_remoto: 3 },
                { dir: h.corridas[1].dir, estado: "pendiente", propietario: "sin_cuenta" }] });
            var d = createTemporaryObject(compDashboard, raiz, { appState: estadoComun, historial: h, subidas: sub });
            compare(findChild(d, "estadoBase_0").children[1].text, "En la base");
            compare(findChild(d, "estadoBase_1").children[1].text, "Sin cuenta");
            sub.elementos = [];
            compare(findChild(d, "estadoBase_0").children[1].text, "Solo local", "no está en la cola ni en la base");
            var sinCola = createTemporaryObject(compDashboard, raiz, { appState: estadoComun, historial: h });
            verify(!findChild(sinCola, "estadoBase_0").visible, "sin backend de la cola no se muestra");
        }

        function test_resultados_sin_cuenta_y_otra_cuenta() {
            var h = createTemporaryObject(compHistorial, raiz);
            var dir = h.corridas[0].dir;
            var sub = createTemporaryObject(compSubidas, raiz, { elementos: [{ dir: dir, estado: "pendiente", propietario: "sin_cuenta", ultimo_error: "" }] });
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h, subidas: sub });
            compare(findChild(r, "botonSubir").text, "Subir a mi cuenta");
            compare(findChild(r, "botonSubir").icono, "subir");
            verify(findChild(r, "estadoSubida").text.indexOf("sin cuenta") >= 0);
            compare(findChild(r, "estadoBaseResultados").children[1].text, "Sin cuenta");
            sub.elementos = [{ dir: dir, estado: "pendiente", propietario: "otra", ultimo_error: "" }];
            verify(!findChild(r, "botonSubir").visible, "la de otra cuenta no se puede subir desde aquí");
        }

        function test_recuperar_contrasena_con_codigo() {
            var s = createTemporaryObject(compSesion, raiz);
            var a = createTemporaryObject(compAcceso, raiz, { sesion: s });
            mouseClick(findChild(a, "enlaceRecuperar"));
            compare(a.modo, "recuperar");
            verify(!findChild(a, "campoContrasena").visible, "primero solo se pide el correo");
            escribir(findChild(a, "campoCorreo"), "ana@ejemplo.com");
            compare(findChild(a, "botonEnviar").text, "Enviar código");
            mouseClick(findChild(a, "botonEnviar"));
            compare(a.pasoRecuperar, "codigo");
            verify(findChild(a, "campoCodigo").visible && findChild(a, "campoContrasena").visible);
            escribir(findChild(a, "campoCodigo"), "123456");
            escribir(findChild(a, "campoContrasena"), "nueva-clave");
            escribir(findChild(a, "campoConfirmacion"), "otra");
            compare(findChild(a, "botonEnviar").enabled, false, "las contraseñas deben coincidir");
            escribir(findChild(a, "campoConfirmacion"), "nueva-clave");
            compare(findChild(a, "botonEnviar").text, "Cambiar contraseña");
            mouseClick(findChild(a, "botonEnviar"));
            compare(s.recuperaciones.length, 1);
            compare(s.recuperaciones[0].nueva, "nueva-clave");
            compare(s.estado, "activa");
        }

        function test_dialogo_mi_cuenta() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                             historial: createTemporaryObject(compHistorial, raiz), sesion: s });
            var d = findChild(w, "dialogoCuenta");
            d.open();
            tryCompare(d, "opened", true);
            compare(findChild(d.contentItem, "campoNombreCuenta").text, "Ana Pérez");
            findChild(d.contentItem, "campoNombreCuenta").text = "Ana P.";
            findChild(d.contentItem, "botonGuardarNombre").clicked();
            compare(s.nombre, "Ana P.");
            compare(findChild(d.contentItem, "mensajeCuenta").text, "Nombre actualizado.");
            findChild(d.contentItem, "campoContrasenaActual").text = "mala";
            findChild(d.contentItem, "campoContrasenaNueva").text = "nueva-clave";
            findChild(d.contentItem, "campoContrasenaRepetida").text = "nueva-clave";
            findChild(d.contentItem, "botonCambiarContrasena").clicked();
            compare(findChild(d.contentItem, "mensajeCuenta").text, "La contraseña actual no es correcta.");
            d.close();
            w.close();
        }

        function test_resultados_de_una_corrida_de_la_base() {
            var rem = createTemporaryObject(compRemoto, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, remoto: rem, modoRemoto: true,
                                                                  subidas: createTemporaryObject(compSubidas, raiz) });
            var volvio = 0;
            r.volverSolicitado.connect(function () { volvio++; });
            rem.cargarDetalle(1);
            compare(r.hayCorrida, false);
            compare(findChild(r, "tarjetaVacia").title, "Cargando la corrida desde la base de datos…");
            rem.entregarDetalle(1);
            compare(r.hayCorrida, true);
            verify(findChild(r, "cabeceraRemota").visible);
            verify(!findChild(r, "selectorCorrida").visible, "sin selector de carpetas locales");
            verify(!findChild(r, "botonSubir").visible && !findChild(r, "estadoSubida").visible, "sin controles de subida");
            compare(findChild(r, "statCpuProceso").value, "12.5 %");
            verify(findChild(r, "statEnergia").value.indexOf("2.5000 Wh") === 0);
            var borrar = findChild(r, "botonBorrarRemota");
            verify(borrar.visible, "la corrida propia se puede borrar");
            borrar.clicked();
            var dlg = findChild(r, "confirmarBorrar");
            tryCompare(dlg, "opened", true);
            dlg.accept();
            compare(rem.borrados[0], 1);
            rem.corridaBorrada(1, "");
            compare(volvio, 1, "al borrarla vuelve a la base de datos");
            rem.entregarDetalle(2);
            verify(!findChild(r, "botonBorrarRemota").visible, "la ajena no");
        }

        function test_base_de_datos_ver_y_comparar() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var rem = createTemporaryObject(compRemoto, raiz);
            var p = createTemporaryObject(compBaseDatos, raiz, { appState: estadoComun, remoto: rem, sesion: s,
                                                                 subidas: createTemporaryObject(compSubidas, raiz) });
            var vistas = [], comparadas = null;
            p.verCorrida.connect(function (id) { vistas.push(id); });
            p.compararSolicitado.connect(function (ids) { comparadas = ids; });
            findChild(p, "ver_2").clicked();
            compare(vistas[0], 2);
            verify(!findChild(p, "botonComparar").enabled, "comparar pide al menos 2");
            findChild(p, "comparar_1").toggle(); findChild(p, "comparar_1").toggled();
            findChild(p, "comparar_2").toggle(); findChild(p, "comparar_2").toggled();
            compare(p.seleccion.length, 2);
            verify(findChild(p, "botonComparar").enabled);
            findChild(p, "botonComparar").clicked();
            compare(comparadas.length, 2);
        }

        // Fila de la tabla de Comparar por su título
        function filaComparar(c, titulo) {
            for (var i = 0; i < c.filas.length; i++)
                if (c.filas[i].t === titulo) return c.filas[i];
            return null;
        }

        function test_pagina_comparar() {
            var rem = createTemporaryObject(compRemoto, raiz);
            rem.cargarComparacion([1, 2]);
            var c = createTemporaryObject(compComparar, raiz, { appState: estadoComun, remoto: rem });
            verify(findChild(c, "tablaComparacion").visible);
            compare(c.corridas.length, 2);
            compare(filaComparar(c, "Energía del entrenamiento").f(c.corridas[0]), "2.5000 Wh", "energía del entrenamiento");
            compare(filaComparar(c, "Energía por minuto de entrenamiento").f(c.corridas[1]), "0.5000 Wh/min", "energía por minuto (5 Wh en 10 min)");
            compare(filaComparar(c, "CPU").f(c.corridas[0]), "CPU de prueba");
            compare(findChild(c, "graficaPotencia").series.length, 2, "una serie por corrida");
            // Inferencia (D-014): filas al final, aparte del entrenamiento
            var fila = function (t) { for (var i = 0; i < c.filas.length; i++) if (c.filas[i].t === t) return c.filas[i]; return null; };
            compare(fila("Inferencia").f(c.corridas[0]), "Sin inferencia");
            compare(fila("Energía por predicción").f(c.corridas[0]), "—");
            var conInf = { col_inferencia_activada: true, col_energia_inferencia_wh: 0.75, col_n_predicciones: 24000000,
                           col_energia_por_prediccion_j: 0.0001125, col_tiempo_por_prediccion_us: 2.5167 };
            compare(fila("Energía de la inferencia").f(conInf), "0.7500 Wh", "de las columnas de la base");
            compare(fila("Predicciones").f(conInf), "24000000");
            compare(fila("Energía por predicción").f(conInf), "1.125e-4 J");
            compare(fila("Tiempo por predicción").f(conInf), "2.517 µs");
            var conError = { inferencia: { activada: true, motivo_sin_por_prediccion: "error de inferir" } };
            compare(fila("Energía por predicción").f(conError), "— (error de inferir)", "por prediccion null: el motivo");
            compare(fila("Tiempo por predicción").f(conError), "— (error de inferir)");
            compare(findChild(c, "graficaPotencia").unidad, "W", "la gráfica rotula su escala");
        }

        function test_comparar_separa_las_familias_de_metricas() {
            var rem = createTemporaryObject(compRemoto, raiz);
            rem.cargarComparacion([1, 2]);
            var c = createTemporaryObject(compComparar, raiz, { appState: estadoComun, remoto: rem });
            var secciones = c.filas.filter(function (f) { return f.seccion !== undefined; }).map(function (f) { return f.seccion; });
            compare(secciones.length, 7);
            // Cada métrica queda bajo su sección: duración (experimento), accuracy (modelo),
            // energía (monitor físico) y CPU (monitor de software)
            function seccionDe(titulo) {
                var actual = "";
                for (var i = 0; i < c.filas.length; i++) {
                    if (c.filas[i].seccion !== undefined) actual = c.filas[i].seccion;
                    else if (c.filas[i].t === titulo) return actual;
                }
                return null;
            }
            compare(seccionDe("Duración del entrenamiento"), "Experimento");
            compare(seccionDe("Exactitud en validación"), "Desempeño del modelo");
            verify(seccionDe("Energía del entrenamiento").indexOf("monitor físico de energía") >= 0);
            verify(seccionDe("CPU media del proceso").indexOf("monitor de software") >= 0);
            compare(filaComparar(c, "Exactitud en validación").f(c.corridas[0]), "83.00 %");
        }

        function test_main_navega_al_detalle_y_a_comparar() {
            var s = createTemporaryObject(compSesion, raiz);
            s.iniciarSesion("ana@ejemplo.com", "secreta1");
            var rem = createTemporaryObject(compRemoto, raiz);
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                             historial: createTemporaryObject(compHistorial, raiz), sesion: s,
                                                             subidas: createTemporaryObject(compSubidas, raiz), remoto: rem });
            w.currentPage = "base_datos";
            wait(30);
            findChild(w.contentItem, "ver_1").clicked();
            compare(w.currentPage, "corrida_remota");
            compare(rem.detallesPedidos[0], 1);
            w.currentPage = "base_datos";
            wait(30);
            findChild(w.contentItem, "comparar_1").toggle(); findChild(w.contentItem, "comparar_1").toggled();
            findChild(w.contentItem, "comparar_2").toggle(); findChild(w.contentItem, "comparar_2").toggled();
            findChild(w.contentItem, "botonComparar").clicked();
            compare(w.currentPage, "comparar");
            compare(rem.comparacionesPedidas.length, 1);
            w.close();
        }

        function test_main_sin_sesion() {
            var w = createTemporaryObject(compMain, raiz, { cliente: createTemporaryObject(compCliente, raiz),
                                                             historial: createTemporaryObject(compHistorial, raiz) });
            compare(w.inicialesUsuario, "ML", "sin backend de sesión se mantiene la vista anterior");
            w.close();
        }
    }
}
