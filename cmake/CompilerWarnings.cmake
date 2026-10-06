# Applies the project's warning set (and optional sanitizers) to a target.
function(vly_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wformat=2
        -Wnull-dereference
        -Wdouble-promotion)

    if(VLY_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    if(VLY_SANITIZE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
