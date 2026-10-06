cmake_minimum_required(VERSION 3.25)

project(vly-manager
    VERSION 0.1.0
    DESCRIPTION "Fast, practical process manager for Linux"
    HOMEPAGE_URL "https://github.com/zzVini-br/vly-manager"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(VLY_WERROR "Treat compiler warnings as errors" OFF)
option(VLY_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)

add_subdirectory(core)

