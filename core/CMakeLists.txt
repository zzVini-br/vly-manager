add_library(vly_core STATIC
    src/cpu.c
    src/kill.c
    src/proc_reader.c
    src/snapshot.c
    src/stat_parser.c
    src/tree.c
    src/version.c)
add_library(vly::core ALIAS vly_core)

set_target_properties(vly_core PROPERTIES OUTPUT_NAME vly)

target_include_directories(vly_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(vly_core PRIVATE
    VLY_VERSION_STRING="${PROJECT_VERSION}")

# glibc only wraps the pidfd system calls from 2.36 on; older ones get a
# fallback in kill.c.
include(CheckIncludeFile)
check_include_file(sys/pidfd.h VLY_HAVE_SYS_PIDFD_H)
if(VLY_HAVE_SYS_PIDFD_H)
    target_compile_definitions(vly_core PRIVATE VLY_HAVE_SYS_PIDFD_H)
endif()

vly_set_warnings(vly_core)
