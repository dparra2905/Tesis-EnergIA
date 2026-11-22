import QtQuick
import QtTest
import "../../src/ui/qml"
import "../../src/ui/qml/components"
import "../../src/ui/qml/pages"

// D-013: botón "Exportar modelo" en Resultados. Con dobles de prueba y DATOS INVENTADOS.
Item {
    id: raiz
    width: 1300
    height: 1000

    AppState { id: estadoComun }
    Component { id: compHistorial; HistorialFalso {} }
    Component { id: compRemoto; RemotoFalso {} }
    Component { id: compResultados; Resultados { anchors.fill: parent } }

    TestCase {
        name: "ExportarModelo"
        when: windowShown

        function cleanup() { estadoComun.corridaActiva = false; }

        function historialConModelo(avisos) {
            var h = createTemporaryObject(compHistorial, raiz);
            var info = {};
            info[h.corridas[0].dir] = { exportable: true, formato: "libtorch_torch_save", archivo: "modelo_libtorch.pt",
                                        columnas_conocidas: avisos.length === 0, datos_sinteticos: false, avisos: avisos };
            h.infoModelos = info;
            return h;
        }

        function test_sin_modelo_exportable_no_hay_boton() {
            var h = createTemporaryObject(compHistorial, raiz);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            verify(!findChild(r, "botonExportarModelo").visible);
        }

        function test_exporta_el_modelo_y_muestra_el_resultado() {
            var h = historialConModelo([]);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            var b = findChild(r, "botonExportarModelo");
            verify(b.visible && b.enabled);
            verify(!findChild(r, "avisosModelo").visible, "sin avisos");
            compare(r.nombreZipModelo(), "exp_a_corrida_002_modelo.zip");
            h.modeloExportado(h.corridas[0].dir, "C:\\x\\m.zip", "", []);
            compare(r.mensajeExportacion, "Modelo exportado: C:\\x\\m.zip");
            h.modeloExportado(h.corridas[0].dir, "C:\\x\\m.zip", "No se pudo escribir", []);
            verify(r.mensajeExportacion.indexOf("No se pudo exportar el modelo") === 0);
        }

        function test_corrida_vieja_muestra_el_aviso() {
            var h = historialConModelo(["La corrida es anterior a columnas_entrada: ..."]);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            verify(findChild(r, "botonExportarModelo").visible, "se exporta igual");
            verify(findChild(r, "avisosModelo").visible);
            verify(findChild(r, "avisosModelo").text.indexOf("columnas_entrada") >= 0);
        }

        function test_no_se_exporta_durante_una_corrida() {
            var h = historialConModelo([]);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h });
            estadoComun.corridaActiva = true;
            verify(!findChild(r, "botonExportarModelo").enabled);
            estadoComun.corridaActiva = false;
            verify(findChild(r, "botonExportarModelo").enabled);
        }

        function test_vista_remota_sin_boton() {
            var h = historialConModelo([]);
            var r = createTemporaryObject(compResultados, raiz, { appState: estadoComun, historial: h,
                                                                  remoto: createTemporaryObject(compRemoto, raiz), modoRemoto: true });
            verify(!findChild(r, "botonExportarModelo").visible, "el modelo no está en la base");
        }
    }
}
