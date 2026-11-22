cmake_minimum_required(VERSION 3.16)
project(ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
qt_standard_project_setup()
# El modulo QML se incrusta bajo :/qt/qml/, que siempre esta en la ruta de importacion: ui.exe
# encuentra el modulo EnergIA sin depender de la carpeta de compilacion junto al ejecutable.
qt_policy(SET QTP0001 NEW)

# Cliente del proceso de ejecucion (D-010) y de la base de datos (D-011): solo Qt Core y
# Network, para poder probarlo sin interfaz. Usa el lector de encabezados CSV de
# src/execution (energia_ejecucion).
add_library(energia_cliente STATIC
    backend/ClienteOrquestador.cpp backend/ClienteOrquestador.h
    backend/LectorMetricasSistema.cpp backend/LectorMetricasSistema.h
    backend/LectorMetricasEnergia.cpp backend/LectorMetricasEnergia.h
    backend/Historial.cpp backend/Historial.h
    backend/ClienteSupabase.cpp backend/ClienteSupabase.h
    backend/ProteccionLocal.cpp backend/ProteccionLocal.h
    backend/PaqueteCorrida.cpp backend/PaqueteCorrida.h
    backend/ExportacionModelo.cpp backend/ExportacionModelo.h
    backend/ZipSimple.cpp backend/ZipSimple.h
    backend/ColaSubidas.cpp backend/ColaSubidas.h
    backend/HistorialRemoto.cpp backend/HistorialRemoto.h)
set_target_properties(energia_cliente PROPERTIES AUTOMOC ON)
target_include_directories(energia_cliente PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backend)
target_link_libraries(energia_cliente PUBLIC Qt6::Core Qt6::Network energia_analisis PRIVATE energia_ejecucion)
if(WIN32)
    target_compile_definitions(energia_cliente PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # DPAPI para cifrar el refresh_token (ProteccionLocal)
    target_link_libraries(energia_cliente PRIVATE crypt32)
endif()
# Ubicacion por defecto del monitor fisico de energia (la misma que usa el orquestador)
target_compile_definitions(energia_cliente PRIVATE
    "ENERGIA_DIR_MEDIDORES=\"${CMAKE_SOURCE_DIR}/src/monitoring/hardware_monitor\"")

qt_add_executable(ui
    main.cpp
)
# ui.exe junto a energia_orquestador.exe y las DLL de modelos: por defecto los busca en su carpeta
if(DEFINED ENERGIA_DIR_BIN)
    set_target_properties(ui PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
    # Configuracion del laboratorio junto a ui.exe (se puede indicar otra con --laboratorio)
    configure_file(${CMAKE_SOURCE_DIR}/config/laboratorio.json ${ENERGIA_DIR_BIN}/laboratorio.json COPYONLY)
endif()

qt_add_qml_module(ui
    URI EnergIA
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/VentanaAcceso.qml
        qml/pages/Acceso.qml
        qml/pages/Dashboard.qml
        qml/pages/NuevoExperimento.qml
        qml/pages/ExperimentoEnEjecucion.qml
        qml/pages/Resultados.qml
        qml/pages/Configuracion.qml
        qml/pages/BaseDatos.qml
        qml/pages/Comparar.qml
        qml/components/Sidebar.qml
        qml/components/SidebarItem.qml
        qml/components/StatusPill.qml
        qml/components/StatCard.qml
        qml/components/CalidadModelo.qml
        qml/components/Casilla.qml
        qml/components/PaletaOscura.qml
        qml/components/Tipografia.js
        qml/components/ResourceBar.qml
        qml/components/MetricChip.qml
        qml/components/SummaryItem.qml
        qml/components/ToggleSwitch.qml
        qml/components/LineChart.qml
        qml/components/Sparkline.qml
        qml/components/EventLogItem.qml
        qml/components/AppState.qml
        qml/components/SectionCard.qml
        qml/components/MonitorBadge.qml
        qml/components/MonitorStatusRow.qml
        qml/components/LiveMetricCard.qml
        qml/components/MetricGroupSelector.qml
        qml/components/NotificationBell.qml
        qml/components/NotificationItem.qml
        qml/components/NotificationCenter.qml
        qml/components/HiperparametroCampo.qml
        qml/components/EstadoBase.qml
        qml/components/DialogoCuenta.qml
        qml/components/Icono.qml
        qml/components/Iconos.js
        qml/components/BotonIcono.qml
        qml/components/Enlace.qml
        qml/components/Ayuda.qml
        qml/components/Ayudas.js
        qml/components/Veredicto.qml
        qml/components/DialogoNota.qml
)

target_link_libraries(ui PRIVATE
    energia_cliente
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
