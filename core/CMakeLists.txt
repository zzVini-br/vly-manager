add_library(vly_core STATIC
    src/version.c)
add_library(vly::core ALIAS vly_core)

set_target_properties(vly_core PROPERTIES OUTPUT_NAME vly)

target_include_directories(vly_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(vly_core PRIVATE
    VLY_VERSION_STRING="${PROJECT_VERSION}")

vly_set_warnings(vly_core)
