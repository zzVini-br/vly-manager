add_executable(vly
    cmd_kill.c
    cmd_list.c
    format.c
    main.c
    match.c
    tui/theme.c
    tui/tui.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

target_include_directories(vly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vly PRIVATE vly::core PkgConfig::NCURSESW)

vly_set_warnings(vly)

install(TARGETS vly RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
