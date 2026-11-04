# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/include")

# The renderer is pure number crunching: build it optimized even in debug builds.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/fx_draw.c
    ${CMAKE_SOURCE_DIR}/src/scenes.c
    ${CMAKE_SOURCE_DIR}/src/lyric_fx.c
    PROPERTIES COMPILE_OPTIONS "-O2;-ffast-math")
