# Ejecución de modelos: contrato de DLL (v4), catálogo de modelos, carga de la DLL,
# datos y hilo de trabajo de la corrida.

# Contrato entre el orquestador y las DLL de modelos (solo cabeceras)
add_library(energia_contrato INTERFACE)
target_include_directories(energia_contrato INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/contract)

find_package(Qt6 REQUIRED COMPONENTS Core)

# Catálogo de modelos (Qt Core, sin interfaz): descubre las DLL que cumplen el contrato v4
add_library(energia_catalogo STATIC CatalogoModelos.cpp CatalogoModelos.h)
target_include_directories(energia_catalogo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(energia_catalogo PUBLIC energia_contrato Qt6::Core)

# Ejecución de una corrida (Qt Core, sin interfaz)
add_library(energia_ejecucion STATIC
    Reloj.cpp Reloj.h
    DatosCsv.cpp DatosCsv.h
    FormatosModelo.h
    DllModelo.cpp DllModelo.h
    MetricasModelo.cpp MetricasModelo.h
    Trabajador.cpp Trabajador.h)
set_target_properties(energia_ejecucion PROPERTIES AUTOMOC ON)
target_include_directories(energia_ejecucion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(energia_ejecucion PUBLIC energia_catalogo Qt6::Core)

if(WIN32)
    target_compile_definitions(energia_catalogo PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(energia_ejecucion PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(models/prueba)
