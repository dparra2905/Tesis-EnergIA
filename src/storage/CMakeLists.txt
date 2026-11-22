# Almacenamiento de las corridas (Qt Core)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(energia_almacenamiento STATIC Corridas.cpp Corridas.h)
target_include_directories(energia_almacenamiento PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(energia_almacenamiento PUBLIC Qt6::Core)
