# Resumen de las mediciones de una corrida terminada (F9). Solo Qt Core.
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(energia_analisis STATIC ResumenCorrida.cpp ResumenCorrida.h)
target_include_directories(energia_analisis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Fases de la linea de tiempo (columna 'fase'): las mismas reglas que la energia por fase
target_link_libraries(energia_analisis PUBLIC energia_sync Qt6::Core)
