# Post-proceso de la energia del experimento (D-008, F7). Solo Qt Core.
find_package(Qt6 REQUIRED COMPONENTS Core)

# Auditoria de corridas (D-012): reglas en AuditoriaReglas.h
add_library(energia_sync STATIC EnergiaExperimento.cpp EnergiaExperimento.h FasesCorrida.cpp FasesCorrida.h
            Auditoria.cpp Auditoria.h AuditoriaReglas.h)
target_include_directories(energia_sync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(energia_sync PUBLIC Qt6::Core)
