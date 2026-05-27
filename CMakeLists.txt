cmake_minimum_required(VERSION 3.20)

project(student_com_kinematic_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(N8RO_RELEASE "C:/n8ro" CACHE PATH "N8RO release root")

add_library(student_com_kinematic_plugin SHARED
    src/StudentComKinematicPlugin.cpp
)

target_include_directories(student_com_kinematic_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${N8RO_RELEASE}/include/astlib
    ${N8RO_RELEASE}/include/astsim
    ${N8RO_RELEASE}/include/astdb
    ${N8RO_RELEASE}/include/n8ro-db
)

target_compile_definitions(student_com_kinematic_plugin PRIVATE
    NDEBUG
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    ARKHEON_ASTLIB_EXPORTS
)

target_link_directories(student_com_kinematic_plugin PRIVATE
    ${N8RO_RELEASE}/lib
)

target_link_libraries(student_com_kinematic_plugin PRIVATE
    arkheon-astlib
    arkheon-astdb
    n8ro-db
    arkheon-astsim
)

set_target_properties(student_com_kinematic_plugin PROPERTIES
    OUTPUT_NAME "student-com-kinematic-plugin"
    PREFIX ""
)
