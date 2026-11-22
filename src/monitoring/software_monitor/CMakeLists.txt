# Monitor de software (D-004): system_meter.exe mide CPU y RAM del sistema, del proceso de
# ejecucion (energia_orquestador, que contiene la DLL; D-010) y de si mismo, sobre la rejilla
# de 1 s que empieza en t_start (D-007). Solo Windows (API Win32, PDH).
# Origen: Back-EnergIA (commit 1ab19f1), compilado alli con
#   g++ -O2 -std=c++17 -static -o system_meter.exe system_meter.cpp -lpdh -lwinmm -lshell32

if(NOT WIN32)
    message(STATUS "system_meter solo se compila en Windows")
    return()
endif()

add_executable(system_meter system_meter.cpp)
target_link_libraries(system_meter PRIVATE pdh winmm shell32)
# Junto a energia_orquestador.exe: el orquestador lo busca en su propia carpeta
set_target_properties(system_meter PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
if(MINGW)
    # Sin dependencias de libstdc++/libgcc en tiempo de ejecucion, como en el original
    target_link_options(system_meter PRIVATE -static)
endif()
