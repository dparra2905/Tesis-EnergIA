find_package(Qt6 REQUIRED COMPONENTS Core)

# Las pruebas que usan Qt necesitan sus DLL en el PATH al ejecutarse. Se antepone el directorio
# de Qt al PATH vigente al ejecutar la prueba (ENVIRONMENT_MODIFICATION). Con la propiedad
# ENVIRONMENT, los ';' del PATH se interpretaban como separadores de lista y el PATH quedaba
# reducido al directorio de Qt.
set(ENERGIA_PATH_PRUEBAS "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
set(ENERGIA_COMPROBAR_SALIDA ${PROJECT_SOURCE_DIR}/tests/cmake/ComprobarSalida.cmake)

# ---- Contrato de DLL v4 contra la DLL de prueba ----
add_executable(test_contrato test_contrato.cpp)
target_link_libraries(test_contrato PRIVATE energia_contrato)
target_compile_definitions(test_contrato PRIVATE "ENERGIA_DLL_PRUEBA=\"$<TARGET_FILE:energia_prueba>\"")
set_target_properties(test_contrato PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
if(WIN32)
    target_compile_definitions(test_contrato PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MINGW)
    target_link_options(test_contrato PRIVATE -static)
endif()
add_dependencies(test_contrato energia_prueba)

add_test(NAME contrato_dll_v4 COMMAND test_contrato)

# ---- DLL que no cumplen el contrato (solo para probar el catalogo) ----
# Quedan fuera de bin/ para no aparecer en el catalogo de la aplicacion.
set(ENERGIA_DIR_DLL_INVALIDAS ${CMAKE_CURRENT_BINARY_DIR}/dlls_invalidas)
foreach(variante Incompleta VersionVieja SinPredecir)
    add_library(dll_invalida_${variante} SHARED dlls/dll_contrato_invalido.cpp)
    set_target_properties(dll_invalida_${variante} PROPERTIES
        OUTPUT_NAME "EnergIA_${variante}"
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_DLL_INVALIDAS}
        LIBRARY_OUTPUT_DIRECTORY ${ENERGIA_DIR_DLL_INVALIDAS})
    if(MINGW)
        target_link_options(dll_invalida_${variante} PRIVATE -static-libgcc -static-libstdc++)
    endif()
endforeach()
target_compile_definitions(dll_invalida_VersionVieja PRIVATE VERSION_VIEJA)
target_compile_definitions(dll_invalida_SinPredecir PRIVATE SIN_PREDECIR)

# ---- Catalogo de modelos ----
add_executable(test_catalogo test_catalogo.cpp)
target_link_libraries(test_catalogo PRIVATE energia_catalogo Qt6::Core)
target_compile_definitions(test_catalogo PRIVATE
    "ENERGIA_DLL_PRUEBA=\"$<TARGET_FILE:energia_prueba>\""
    "ENERGIA_DLL_INCOMPLETA=\"$<TARGET_FILE:dll_invalida_Incompleta>\""
    "ENERGIA_DLL_VERSION_VIEJA=\"$<TARGET_FILE:dll_invalida_VersionVieja>\""
    "ENERGIA_DLL_SIN_PREDECIR=\"$<TARGET_FILE:dll_invalida_SinPredecir>\"")
set_target_properties(test_catalogo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_dependencies(test_catalogo energia_prueba dll_invalida_Incompleta dll_invalida_VersionVieja dll_invalida_SinPredecir)

add_test(NAME catalogo_modelos COMMAND test_catalogo)
set_tests_properties(catalogo_modelos PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- energia_orquestador --catalogo (de punta a punta) ----
# Se exige el codigo de salida exacto ademas del contenido (ver tests/cmake/ComprobarSalida.cmake)
add_test(NAME orquestador_catalogo
         COMMAND ${CMAKE_COMMAND}
                 -DPROGRAMA=$<TARGET_FILE:energia_orquestador>
                 "-DARGUMENTOS=--catalogo|--dir-modelos|${ENERGIA_DIR_BIN}"
                 -DCODIGO=0
                 "-DPATRON=\"clave\": \"prueba\""
                 -P ${ENERGIA_COMPROBAR_SALIDA})
set_tests_properties(orquestador_catalogo PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

add_test(NAME orquestador_sin_dir_modelos
         COMMAND ${CMAKE_COMMAND}
                 -DPROGRAMA=$<TARGET_FILE:energia_orquestador>
                 "-DARGUMENTOS=--catalogo"
                 -DCODIGO=2
                 -P ${ENERGIA_COMPROBAR_SALIDA})
set_tests_properties(orquestador_sin_dir_modelos PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Cliente del orquestador en la interfaz (F4), sin lanzar procesos ----
add_executable(test_cliente test_cliente.cpp)
target_link_libraries(test_cliente PRIVATE energia_cliente Qt6::Core)
set_target_properties(test_cliente PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME cliente_orquestador COMMAND test_cliente)
set_tests_properties(cliente_orquestador PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Metricas del modelo (D-015) contra valores calculados a mano ----
add_executable(test_metricas_modelo test_metricas_modelo.cpp)
target_link_libraries(test_metricas_modelo PRIVATE energia_ejecucion Qt6::Core)
set_target_properties(test_metricas_modelo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME metricas_modelo COMMAND test_metricas_modelo)
set_tests_properties(metricas_modelo PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Energia del experimento (D-008, F7) con tramas inventadas ----
add_executable(test_energia_experimento test_energia_experimento.cpp)
target_link_libraries(test_energia_experimento PRIVATE energia_sync Qt6::Core)
set_target_properties(test_energia_experimento PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME energia_experimento COMMAND test_energia_experimento)
set_tests_properties(energia_experimento PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Auditoria de corridas (D-012) con datos sinteticos ----
add_executable(test_auditoria test_auditoria.cpp)
target_link_libraries(test_auditoria PRIVATE energia_sync Qt6::Core)
set_target_properties(test_auditoria PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME auditoria_corridas COMMAND test_auditoria)
set_tests_properties(auditoria_corridas PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Historial, resumen y exportacion de corridas (F9) ----
add_executable(test_historial test_historial.cpp)
target_link_libraries(test_historial PRIVATE energia_cliente energia_analisis Qt6::Core)
set_target_properties(test_historial PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME historial_corridas COMMAND test_historial)
set_tests_properties(historial_corridas PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# Exportacion del modelo (D-013): .zip con el modelo, modelo.json y LEEME.txt
add_executable(test_exportacion test_exportacion.cpp)
target_link_libraries(test_exportacion PRIVATE energia_cliente Qt6::Core)
set_target_properties(test_exportacion PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME exportacion_modelo COMMAND test_exportacion)
set_tests_properties(exportacion_modelo PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Sesion con la base de datos (D-011), contra un servidor falso local ----
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(test_supabase test_supabase.cpp ServidorSupabaseFalso.h)
target_link_libraries(test_supabase PRIVATE energia_cliente Qt6::Core Qt6::Network)
set_target_properties(test_supabase PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME sesion_supabase COMMAND test_supabase)
set_tests_properties(sesion_supabase PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")

# ---- Paquete de subida y cola de pendientes (D-011), contra un servidor falso local ----
add_executable(test_subidas test_subidas.cpp ServidorSupabaseFalso.h)
target_link_libraries(test_subidas PRIVATE energia_cliente energia_analisis Qt6::Core Qt6::Network)
set_target_properties(test_subidas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
add_test(NAME subidas_base_datos COMMAND test_subidas)
set_tests_properties(subidas_base_datos PROPERTIES ENVIRONMENT_MODIFICATION "${ENERGIA_PATH_PRUEBAS}")
