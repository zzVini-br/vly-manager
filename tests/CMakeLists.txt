function(vly_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE vly::core)
    vly_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

vly_add_test(test_version)
