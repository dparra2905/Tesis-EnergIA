# ComprobarSalida.cmake - Ejecuta un programa y exige su codigo de salida y, si se indica, un
# patron en stdout. Sustituye a PASS_REGULAR_EXPRESSION / WILL_FAIL de CTest, que ignoran el
# codigo de salida (una salida correcta seguida de un fallo pasaria la prueba).
#
# Uso (desde add_test):
#   cmake -DPROGRAMA=<exe> "-DARGUMENTOS=a|b|c" -DCODIGO=<n> "-DPATRON=<regex>" -P ComprobarSalida.cmake
# ARGUMENTOS se separa con '|' para no chocar con las listas de CMake.

if(NOT DEFINED PROGRAMA OR NOT DEFINED CODIGO)
    message(FATAL_ERROR "ComprobarSalida: faltan PROGRAMA o CODIGO")
endif()

set(argumentos "")
if(DEFINED ARGUMENTOS AND NOT ARGUMENTOS STREQUAL "")
    string(REPLACE "|" ";" argumentos "${ARGUMENTOS}")
endif()

execute_process(
    COMMAND "${PROGRAMA}" ${argumentos}
    RESULT_VARIABLE codigo_obtenido
    OUTPUT_VARIABLE salida
    ERROR_VARIABLE errores)

message("${salida}")
if(NOT errores STREQUAL "")
    message("--- stderr ---\n${errores}")
endif()

if(NOT codigo_obtenido STREQUAL "${CODIGO}")
    message(FATAL_ERROR "Codigo de salida ${codigo_obtenido}; se esperaba ${CODIGO}")
endif()
if(DEFINED PATRON AND NOT PATRON STREQUAL "" AND NOT salida MATCHES "${PATRON}")
    message(FATAL_ERROR "La salida no contiene el patron esperado: ${PATRON}")
endif()
message("OK: codigo ${codigo_obtenido}")
