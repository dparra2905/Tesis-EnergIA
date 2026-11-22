# energia_orquestador.exe: proceso de ejecucion sin interfaz (D-010). Solo Qt Core.
find_package(Qt6 REQUIRED COMPONENTS Core)

# Logica de la corrida, separada del main para poder probarla
add_library(energia_experimento STATIC
    ConfigExperimento.cpp ConfigExperimento.h
    Protocolo.cpp Protocolo.h
    ExperimentManager.cpp ExperimentManager.h
    GestorMedidores.cpp GestorMedidores.h)
set_target_properties(energia_experimento PROPERTIES AUTOMOC ON)
target_include_directories(energia_experimento PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(energia_experimento PUBLIC energia_ejecucion energia_almacenamiento energia_sync Qt6::Core)
# Carpeta del monitor fisico de energia (energia_ble.py y su entorno venv/), como en Back-EnergIA
target_compile_definitions(energia_experimento PRIVATE
    "ENERGIA_DIR_MEDIDORES=\"${PROJECT_SOURCE_DIR}/src/monitoring/hardware_monitor\"")

add_executable(energia_orquestador main.cpp)
target_link_libraries(energia_orquestador PRIVATE energia_experimento Qt6::Core)
set_target_properties(energia_orquestador PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
if(WIN32)
    # Ejecutable de consola (no WIN32_EXECUTABLE): sin ventana, salida por stdout/stderr
    target_compile_definitions(energia_experimento PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(energia_orquestador PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
