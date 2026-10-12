# Curses-free view logic, shared by the terminal UI and its tests.
find_package(Threads REQUIRED)

add_library(vly_view STATIC
    match.c
    tui/kill_jobs.c
    tui/quickfind.c
    tui/tree_view.c)
target_include_directories(vly_view PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tui)
target_link_libraries(vly_view PUBLIC vly::core Threads::Threads)
vly_set_warnings(vly_view)

add_executable(vly
    cmd_kill.c
    cmd_list.c
    format.c
    main.c
    tui/theme.c
    tui/tui.c)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

target_include_directories(vly PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vly PRIVATE vly::core vly_view PkgConfig::NCURSESW)

vly_set_warnings(vly)

install(TARGETS vly RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
