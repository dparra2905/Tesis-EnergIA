# DLL de prueba (modelo simulado, sin librerías externas). Solo para pruebas internas:
# no es un modelo real del alcance (D-009) y no debe presentarse como tal en la interfaz.
# Origen: CMakeLists.txt de Back-EnergIA (commit 1ab19f1), sección "DLL de prueba".

add_library(energia_prueba SHARED energia_dll_stub.cpp)
target_link_libraries(energia_prueba PRIVATE energia_contrato)
target_compile_definitions(energia_prueba PRIVATE ENERGIA_DLL_EXPORTS)
set_target_properties(energia_prueba PROPERTIES
    OUTPUT_NAME "EnergIA_Prueba"
    PREFIX ""                      # MinGW agregaria "lib" al nombre
    CXX_VISIBILITY_PRESET hidden
    RUNTIME_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN}
    LIBRARY_OUTPUT_DIRECTORY ${ENERGIA_DIR_BIN})
if(WIN32)
    target_compile_definitions(energia_prueba PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MINGW)
    # La DLL no depende de libstdc++-6.dll / libgcc_s en tiempo de ejecucion
    target_link_options(energia_prueba PRIVATE -static-libgcc -static-libstdc++)
endif()
