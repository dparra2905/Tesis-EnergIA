import QtQuick
import QtTest
import "../../src/ui/qml/components"
import "../../src/ui/qml/components/Tipografia.js" as Tipografia

// Regresión visual básica de la escala de texto y de los contenedores que la alojan.
Item {
    id: raiz
    width: 1200
    height: 800

    Component { id: itemNavegacion; SidebarItem { label: "Nuevo experimento"; width: 238 } }
    Component { id: tarjetaEstadistica; StatCard { width: 260; title: "Energía del experimento"; value: "25.1234 Wh"; caption: "Cobertura de las mediciones" } }
    Component { id: tarjetaSeccion; SectionCard { width: 500; title: "Uso de recursos computacionales"; subtitle: "Medición durante el entrenamiento del modelo" } }
    Component { id: insigniaMonitor; MonitorBadge { text: "Monitor de software" } }
    Component { id: barraLateral; Sidebar { width: 240; height: 800 } }
    Component { id: selectorEstrecho; MetricGroupSelector {
        width: 160; title: "Uso de recursos computacionales"; monitorName: "Monitor de software"
        metrics: [{label: "CPU"}, {label: "RAM"}]
    }}
    Component { id: tarjetaCpu; StatCard {
        width: 240; title: "CPU del proceso de ejecuci?n (promedio)"; value: "43.1 %"
    }}
    Component { id: tarjetaRam; StatCard {
        width: 240; title: "RAM del sistema"; value: "63.7 %"
    }}


    TestCase {
        name: "Tipografia"
        when: windowShown

        function test_escala_jerarquica_y_legible() {
            verify(Tipografia.detalle >= 11);
            verify(Tipografia.auxiliar >= 12);
            verify(Tipografia.cuerpo >= 13);
            verify(Tipografia.subtitulo > Tipografia.cuerpo);
            verify(Tipografia.tituloSeccion > Tipografia.subtitulo);
            verify(Tipografia.tituloPrincipal > Tipografia.tituloSeccion);
        }


        function test_cpu_ram_en_columnas_y_con_espaciado() {
            var selector = createTemporaryObject(selectorEstrecho, raiz);
            verify(selector !== null);
            var flujo = findChild(selector, "flujoMetricas");
            verify(flujo !== null);
            tryVerify(function() { return flujo.height >= 70; },
                      1000, "CPU y RAM se acomodan en dos l?neas sin superponerse");
            var alturaEstrecha = selector.implicitHeight;
            selector.width = 360;
            tryVerify(function() { return selector.implicitHeight < alturaEstrecha; },
                      1000, "los chips regresan a una fila cuando hay espacio");
        }

        function test_titulos_largos_no_desplazan_valores() {
            var cpu = createTemporaryObject(tarjetaCpu, raiz);
            var ram = createTemporaryObject(tarjetaRam, raiz);
            verify(cpu !== null && ram !== null);
            var valorCpu = findChild(cpu, "valorMetrica");
            var valorRam = findChild(ram, "valorMetrica");
            verify(valorCpu !== null && valorRam !== null);
            compare(valorCpu.y, valorRam.y,
                    "CPU y RAM empiezan su valor a la misma altura aunque sus t?tulos tengan distinta longitud");
        }

        function test_logo_de_energia_visible_y_mas_grande() {
            var barra = createTemporaryObject(barraLateral, raiz);
            verify(barra !== null);
            var marca = findChild(barra, "marcaEnergIA");
            verify(marca !== null);
            verify(marca.implicitHeight >= 44, "el logotipo reserva espacio para el icono de 44 px");
        }

        function test_elementos_se_adaptan_a_las_letras() {
            var nav = createTemporaryObject(itemNavegacion, raiz);
            verify(nav.height >= 44, "el menú mantiene una altura cómoda");
            var stat = createTemporaryObject(tarjetaEstadistica, raiz);
            verify(stat.implicitHeight >= 105, "la tarjeta deja espacio al valor y a la nota");
            var section = createTemporaryObject(tarjetaSeccion, raiz);
            verify(section.implicitHeight > 50, "la sección reserva espacio para título y subtítulo");
            var badge = createTemporaryObject(insigniaMonitor, raiz);
            verify(badge.implicitHeight >= 27, "la insignia no comprime su texto");
        }
    }
}
