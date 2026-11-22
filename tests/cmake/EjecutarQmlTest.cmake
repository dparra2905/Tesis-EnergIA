# EjecutarQmlTest.cmake - Ejecuta qmltestrunner y vuelca su informe en la salida de CTest.
# En Windows qmltestrunner no escribe en la consola heredada; el informe se pide en un archivo.
#
# Uso: cmake -DQMLTESTRUNNER=<exe> -DENTRADA=<dir de pruebas> -DINFORME=<archivo> -P EjecutarQmlTest.cmake

if(NOT DEFINED QMLTESTRUNNER OR NOT DEFINED ENTRADA OR NOT DEFINED INFORME)
    message(FATAL_ERROR "EjecutarQmlTest: faltan QMLTESTRUNNER, ENTRADA o INFORME")
endif()

file(REMOVE "${INFORME}")
execute_process(
    COMMAND "${QMLTESTRUNNER}" -input "${ENTRADA}" -o "${INFORME},txt"
    RESULT_VARIABLE resultado)

if(EXISTS "${INFORME}")
    file(READ "${INFORME}" informe)
    message("${informe}")
else()
    message("qmltestrunner no genero el informe ${INFORME}")
endif()

if(NOT resultado EQUAL 0)
    message(FATAL_ERROR "qmltestrunner termino con ${resultado}")
endif()
# Un error de JavaScript en una vista (funcion inexistente, propiedad indefinida) solo aparece
# como aviso y no hace fallar la prueba: se trata como fallo
if(informe MATCHES "TypeError|ReferenceError")
    message(FATAL_ERROR "Errores de JavaScript en las vistas QML (ver TypeError/ReferenceError arriba)")
endif()
