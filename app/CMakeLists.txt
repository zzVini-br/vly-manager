add_executable(vly
    format.c
    main.c
    match.c)

target_link_libraries(vly PRIVATE vly::core)

vly_set_warnings(vly)

install(TARGETS vly RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
