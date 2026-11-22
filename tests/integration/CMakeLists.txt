find_package(Qt6 REQUIRED COMPONENTS Core)
set(ENERGIA_PATH_PRUEBAS "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# Corrida completa con energia_orquestador --ejecutar y la DLL de prueba (F3).
#
# test_corrida (C++) compila, pero en el equipo de desarrollo actual Smart App Control
# bloquea su ejecucion (Code Integrity, evento 3077). Las mismas comprobaciones se ejecutan
# con test_corrida.py: Python solo conduce la prueba; el codigo bajo prueba es
# energia_orquestador.exe.
add_executable(test_corrida test_corrida.cpp)
target_link_libraries(test_corrida PRIVATE Qt6::Core)
target_compile_definitions(test_corrida PRIVATE
    "ENERGIA_ORQUESTADOR=\"$<TARGET_FILE:energia_orquestador>\""
    "ENERGIA_DLL_PRUEBA=\"$<TARGET_FILE:energia_prueba>\"")
set_target_properties(test_corrida PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_dependencies(test_corrida energia_orquestador energia_prueba)

option(ENERGIA_EJECUTAR_TEST_CORRIDA_CPP
       "Registrar en CTest test_corrida.exe (desactivar donde Smart App Control lo bloquea)" ON)
if(ENERGIA_EJECUTAR_TEST_CORRIDA_CPP)
    add_test(NAME corrida_orquestador_cpp COMMAND test_corrida)
    set_tests_properties(corrida_orquestador_cpp PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}" TIMEOUT 180)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME corrida_orquestador_py
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_corrida.py
                     --orquestador $<TARGET_FILE:energia_orquestador>
                     --dll $<TARGET_FILE:energia_prueba>
                     --energia-ble ${PROJECT_SOURCE_DIR}/src/monitoring/hardware_monitor/energia_ble.py)
    set_tests_properties(corrida_orquestador_py PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}" TIMEOUT 180)
else()
    message(WARNING "No se encontro Python 3: no se registra la prueba corrida_orquestador_py")
endif()

# Cliente de la interfaz (ClienteOrquestador) contra el orquestador real (F4): catalogo,
# corrida, cancelacion, detencion forzada, fallos del proceso y cierre de la interfaz (D-010).
add_executable(test_cliente_orquestador test_cliente_orquestador.cpp)
target_link_libraries(test_cliente_orquestador PRIVATE energia_cliente Qt6::Core)
target_compile_definitions(test_cliente_orquestador PRIVATE
    "ENERGIA_ORQUESTADOR=\"$<TARGET_FILE:energia_orquestador>\""
    "ENERGIA_DLL_PRUEBA=\"$<TARGET_FILE:energia_prueba>\""
    "ENERGIA_CMAKE=\"${CMAKE_COMMAND}\"")
set_target_properties(test_cliente_orquestador PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_dependencies(test_cliente_orquestador energia_orquestador energia_prueba)
add_test(NAME cliente_con_orquestador COMMAND test_cliente_orquestador)
set_tests_properties(cliente_con_orquestador PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}" TIMEOUT 300)

# Monitor de software (F5): system_meter.exe conducido desde Python
if(Python3_Interpreter_FOUND AND TARGET system_meter)
    add_test(NAME system_meter_py
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_system_meter.py
                     --medidor $<TARGET_FILE:system_meter>)
    set_tests_properties(system_meter_py PROPERTIES TIMEOUT 180)
endif()

# Metricas en vivo en el cliente de la interfaz con el system_meter real (F5)
if(TARGET system_meter)
    add_executable(test_metricas_en_vivo test_metricas_en_vivo.cpp)
    target_link_libraries(test_metricas_en_vivo PRIVATE energia_cliente Qt6::Core)
    target_compile_definitions(test_metricas_en_vivo PRIVATE "ENERGIA_SYSTEM_METER=\"$<TARGET_FILE:system_meter>\"")
    set_target_properties(test_metricas_en_vivo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
    add_dependencies(test_metricas_en_vivo system_meter)
    add_test(NAME metricas_en_vivo COMMAND test_metricas_en_vivo)
    set_tests_properties(metricas_en_vivo PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}" TIMEOUT 120)
endif()

# Monitor fisico de energia (F6): partes de energia_ble.py que no necesitan el Atorch
if(Python3_Interpreter_FOUND)
    add_test(NAME energia_ble_py
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_energia_ble.py
                     --script ${PROJECT_SOURCE_DIR}/src/monitoring/hardware_monitor/energia_ble.py)
    set_tests_properties(energia_ble_py PROPERTIES TIMEOUT 60)
endif()

# Arbol de decision real (F8): solo si se indica la DLL compilada con MSVC
# (src/execution/models/compilar_dlls.bat). Sin ella, la prueba no se registra.
set(ENERGIA_DLL_ARBOL "" CACHE FILEPATH "EnergIA_Arbol.dll compilada con MSVC (opcional, para la prueba arbol_py)")
if(Python3_Interpreter_FOUND AND ENERGIA_DLL_ARBOL)
    add_test(NAME arbol_py
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_arbol.py
                     --orquestador $<TARGET_FILE:energia_orquestador> --dll-arbol ${ENERGIA_DLL_ARBOL})
    set_tests_properties(arbol_py PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}" TIMEOUT 600)
endif()
