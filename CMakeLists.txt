cmake_minimum_required(VERSION 3.16)
project(DataScythe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DATASCYTHE_BUILD_GUI "Build the Qt GUI application" ON)
option(DATASCYTHE_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)
include(cmake/DataScythePackaging.cmake)

add_subdirectory(src)
add_subdirectory(apps)

if(DATASCYTHE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(CPack)