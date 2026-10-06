function(vly_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE vly::core)
    target_compile_definitions(${name} PRIVATE
        VLY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    vly_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

vly_add_test(test_cpu)
vly_add_test(test_snapshot)
vly_add_test(test_stat_parser)
vly_add_test(test_tree)
vly_add_test(test_version)
