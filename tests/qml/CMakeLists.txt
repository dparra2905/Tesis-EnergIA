# Pruebas de las vistas QML (F4) con qmltestrunner, la herramienta de Qt Quick Test.
# Usan ClienteFalso.qml en lugar del proceso de ejecucion: comprueban las vistas, no el
# orquestador (eso lo hacen las pruebas de tests/integration).
find_package(Qt6 REQUIRED COMPONENTS Core Qml)

if(NOT TARGET Qt6::qmltestrunner)
    message(WARNING "No se encontro qmltestrunner: no se registra la prueba interfaz_qml")
    return()
endif()

add_test(NAME interfaz_qml
         COMMAND ${CMAKE_COMMAND}
                 -DQMLTESTRUNNER=$<TARGET_FILE:Qt6::qmltestrunner>
                 -DENTRADA=${CMAKE_CURRENT_SOURCE_DIR}
                 -DINFORME=${CMAKE_CURRENT_BINARY_DIR}/interfaz_qml.txt
                 -P ${PROJECT_SOURCE_DIR}/tests/cmake/EjecutarQmlTest.cmake)
set_tests_properties(interfaz_qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Fusion"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 120)

# Iconos de la interfaz: rutas SVG válidas, .svg al día con Iconos.js, ningún nombre de icono
# inexistente en el QML y ningún emoji usado como icono (scripts/generar_iconos_svg.py)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME iconos_interfaz
             COMMAND Python3::Interpreter -X utf8 ${PROJECT_SOURCE_DIR}/scripts/generar_iconos_svg.py --comprobar)
    set_tests_properties(iconos_interfaz PROPERTIES TIMEOUT 60)
endif()
