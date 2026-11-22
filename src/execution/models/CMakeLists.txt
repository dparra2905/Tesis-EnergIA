# Origen: Back-EnergIA (https://github.com/dparra2905/Back-EnergIA), commit 1ab19f1, dlls/CMakeLists.txt
# Autor original: David Parra. Adaptado para EnergIA (Una-Humilde-Tesis): rutas del contrato
# (src/execution/contract). MLP (libtorch) incorporado desde la version local de Back-EnergIA
# posterior a 1ab19f1. Proyecto comun en src/execution/models/ (antes en arbol/), con una carpeta
# por modelo: arbol/ y mlp/. Ver docs/ORIGEN_BACKEND.md.
#
# src/execution/models/CMakeLists.txt - DLL de modelos de EnergIA, compiladas con MSVC.
# (models/prueba/ es la DLL de prueba: la compila la raiz con MinGW, no este proyecto.)
#
# No forma parte de la compilacion MinGW de la raiz: se compila con compilar_dlls.bat, que prepara
# MSVC, descarga vcpkg (y con el, mlpack y sus dependencias) y copia las DLL a la carpeta bin/ del
# orquestador.
#
# Arbol (mlpack): dependencias en modo ESTATICO (triplet x64-windows-static-md). mlpack,
# Armadillo, OpenBLAS y LAPACK quedan dentro de EnergIA_Arbol.dll, y LAPACK usa clapack (C puro)
# en vez de la version en Fortran que pediria el modo dinamico.
#
# MLP (libtorch): se descarga el zip oficial de CPU la primera vez (no esta en vcpkg). libtorch
# se distribuye como DLL: se copian junto a EnergIA_MLP.dll y compilar_dlls.bat las lleva a bin/.
cmake_minimum_required(VERSION 3.21)
project(EnergIA_DLLs LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Las DLL de modelos se compilan con MSVC: usa src/execution/models/compilar_dlls.bat")
endif()

# C++20: libtorch 2.14 lo exige. Se usa en TODAS las DLL para que compilen igual.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# El contrato (energia_dll.h) y las ayudas comunes (energia_comun.h) estan en src/execution/contract
set(ENERGIA_RAIZ ${CMAKE_CURRENT_SOURCE_DIR}/../contract)

# Mismas opciones de compilacion para TODAS las DLL de modelos, para que las diferencias de
# energia se deban al algoritmo y no al compilador. Se registran en compilacion_dlls.txt.
#   /O2 /Ob2  optimizacion de velocidad (lo normal en Release)
#   /EHsc     excepciones de C++ (ninguna sale de la DLL: se atrapan en cada funcion)
#   /bigobj   mlpack genera archivos objeto muy grandes
#   /utf-8    los fuentes estan en UTF-8
#   /wd4251 /wd4275  avisos de libtorch sobre clases exportadas (solo ruido, no cambian el codigo)
#   Sin /openmp y sin /arch:AVX2 en NUESTRO codigo. Ojo: libtorch ya viene compilado con su propio
#   OpenMP y sus rutas AVX; eso es parte de la libreria y queda anotado en la tesis.
set(ENERGIA_OPCIONES_MSVC /O2 /Ob2 /EHsc /bigobj /utf-8 /permissive- /Zc:__cplusplus /W3 /wd4251 /wd4275)
set(ENERGIA_DEFINICIONES ENERGIA_DLL_EXPORTS NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
                         ARMA_DONT_USE_OPENMP)

function(energia_dll objetivo nombre_salida fuente)
    add_library(${objetivo} SHARED ${fuente} ${ENERGIA_RAIZ}/energia_dll.h ${ENERGIA_RAIZ}/energia_comun.h)
    target_include_directories(${objetivo} PRIVATE ${ENERGIA_RAIZ})
    target_compile_definitions(${objetivo} PRIVATE ${ENERGIA_DEFINICIONES})
    target_compile_options(${objetivo} PRIVATE ${ENERGIA_OPCIONES_MSVC})
    set_target_properties(${objetivo} PROPERTIES OUTPUT_NAME ${nombre_salida} PREFIX "")
endfunction()

# ---- Arbol de decision (mlpack) ----
find_package(Armadillo CONFIG REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_path(MLPACK_INCLUDE_DIRS "mlpack.hpp" REQUIRED)

energia_dll(energia_arbol EnergIA_Arbol arbol/energia_arbol.cpp)
target_include_directories(energia_arbol PRIVATE ${MLPACK_INCLUDE_DIRS})
target_link_libraries(energia_arbol PRIVATE armadillo cereal::cereal)

# ---- MLP (libtorch) ----
# Version fija, CPU, Release. Si la descarga falla (por ejemplo, porque PyTorch cambio el nombre
# del archivo), baja el zip "LibTorch / Windows / C++ / CPU / Release" desde pytorch.org y ponlo
# en src/execution/models/descargas/ con el nombre que muestra el error.
set(ENERGIA_LIBTORCH_VERSION "2.14.0")
set(ENERGIA_LIBTORCH_ZIP "libtorch-win-shared-with-deps-${ENERGIA_LIBTORCH_VERSION}+cpu.zip")
set(ENERGIA_LIBTORCH_URL "https://download.pytorch.org/libtorch/cpu/libtorch-win-shared-with-deps-${ENERGIA_LIBTORCH_VERSION}%2Bcpu.zip")
set(ENERGIA_LIBTORCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libtorch-${ENERGIA_LIBTORCH_VERSION}")
set(ENERGIA_LIBTORCH_ZIP_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/descargas/${ENERGIA_LIBTORCH_ZIP}")

# Antes este proyecto vivia en arbol/: si libtorch (o su zip) ya se descargo ahi, se usa desde
# ahi en vez de bajarlo otra vez, y se explica como moverlo.
set(ENERGIA_LIBTORCH_DIR_VIEJO "${CMAKE_CURRENT_SOURCE_DIR}/arbol/libtorch-${ENERGIA_LIBTORCH_VERSION}")
set(ENERGIA_LIBTORCH_ZIP_VIEJO "${CMAKE_CURRENT_SOURCE_DIR}/arbol/descargas/${ENERGIA_LIBTORCH_ZIP}")
if(NOT EXISTS "${ENERGIA_LIBTORCH_DIR}/libtorch/share/cmake/Torch/TorchConfig.cmake")
    if(EXISTS "${ENERGIA_LIBTORCH_DIR_VIEJO}/libtorch/share/cmake/Torch/TorchConfig.cmake")
        message(WARNING "Se usa libtorch desde su ubicacion anterior:\n  ${ENERGIA_LIBTORCH_DIR_VIEJO}\n"
            "Puedes moverlo a la nueva (y borrar arbol\\build y arbol\\vcpkg, que ya no se usan):\n"
            "  move \"src\\execution\\models\\arbol\\libtorch-${ENERGIA_LIBTORCH_VERSION}\" \"src\\execution\\models\\\"")
        set(ENERGIA_LIBTORCH_DIR "${ENERGIA_LIBTORCH_DIR_VIEJO}")
    elseif(EXISTS "${ENERGIA_LIBTORCH_ZIP_VIEJO}" AND NOT EXISTS "${ENERGIA_LIBTORCH_ZIP_LOCAL}")
        message(STATUS "Se usa el zip de libtorch ya descargado en ${ENERGIA_LIBTORCH_ZIP_VIEJO}")
        set(ENERGIA_LIBTORCH_ZIP_LOCAL "${ENERGIA_LIBTORCH_ZIP_VIEJO}")
    endif()
endif()

if(NOT EXISTS "${ENERGIA_LIBTORCH_DIR}/libtorch/share/cmake/Torch/TorchConfig.cmake")
    if(NOT EXISTS "${ENERGIA_LIBTORCH_ZIP_LOCAL}")
        message(STATUS "Descargando libtorch ${ENERGIA_LIBTORCH_VERSION} (CPU), varios cientos de MB...")
        file(DOWNLOAD "${ENERGIA_LIBTORCH_URL}" "${ENERGIA_LIBTORCH_ZIP_LOCAL}.parcial"
             SHOW_PROGRESS STATUS estado TLS_VERIFY ON)
        list(GET estado 0 codigo)
        if(NOT codigo EQUAL 0)
            file(REMOVE "${ENERGIA_LIBTORCH_ZIP_LOCAL}.parcial")
            message(FATAL_ERROR "No se pudo descargar libtorch (${estado}).\n"
                "URL: ${ENERGIA_LIBTORCH_URL}\n"
                "Descargalo a mano desde pytorch.org (LibTorch, Windows, C++, CPU, Release) y guardalo como:\n"
                "${ENERGIA_LIBTORCH_ZIP_LOCAL}")
        endif()
        file(RENAME "${ENERGIA_LIBTORCH_ZIP_LOCAL}.parcial" "${ENERGIA_LIBTORCH_ZIP_LOCAL}")
    endif()
    message(STATUS "Descomprimiendo libtorch...")
    file(ARCHIVE_EXTRACT INPUT "${ENERGIA_LIBTORCH_ZIP_LOCAL}" DESTINATION "${ENERGIA_LIBTORCH_DIR}")
endif()

list(APPEND CMAKE_PREFIX_PATH "${ENERGIA_LIBTORCH_DIR}/libtorch")
find_package(Torch REQUIRED)

energia_dll(energia_mlp EnergIA_MLP mlp/energia_mlp.cpp)
target_link_libraries(energia_mlp PRIVATE ${TORCH_LIBRARIES})

# Las DLL de libtorch (torch_cpu, c10, OpenMP de Intel, ...) van junto a EnergIA_MLP.dll
file(GLOB ENERGIA_LIBTORCH_DLLS "${ENERGIA_LIBTORCH_DIR}/libtorch/lib/*.dll")
add_custom_command(TARGET energia_mlp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENERGIA_LIBTORCH_DLLS} $<TARGET_FILE_DIR:energia_mlp>)


# Registro de como se compilo, para la tesis
file(WRITE ${CMAKE_BINARY_DIR}/bin/compilacion_dlls.txt
    "compilador: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
    "tipo: ${CMAKE_BUILD_TYPE}\n"
    "opciones: ${ENERGIA_OPCIONES_MSVC}\n"
    "definiciones: ${ENERGIA_DEFINICIONES}\n"
    "estandar C++: ${CMAKE_CXX_STANDARD}\n"
    "triplet vcpkg: ${VCPKG_TARGET_TRIPLET}\n"
    "libtorch: ${ENERGIA_LIBTORCH_VERSION} (CPU, Release)\n")
