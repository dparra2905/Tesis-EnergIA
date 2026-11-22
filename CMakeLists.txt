cmake_minimum_required(VERSION 3.22)
project(EnergIA LANGUAGES C CXX)

# Raíz del proyecto: compila la interfaz, la ejecución de modelos y las pruebas.
# Los ejecutables y DLL del backend quedan juntos en bin/.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENERGIA_DIR_BIN ${CMAKE_BINARY_DIR}/bin)

enable_testing()

add_subdirectory(src/ui)
add_subdirectory(src/execution)
add_subdirectory(src/storage)
add_subdirectory(src/monitoring/software_monitor)
add_subdirectory(src/sync)
add_subdirectory(src/analysis)
add_subdirectory(src/experiment_manager)
add_subdirectory(tests)
