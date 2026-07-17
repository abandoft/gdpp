function(gdpp_set_project_warnings target)
    if(MSVC)
        # MSVC otherwise decodes UTF-8 sources with the active Windows code page. This
        # is not only a diagnostics problem: non-ASCII GDScript literals embedded in
        # generated C++ can be corrupted or make an /WX build fail.
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
        if(GDPP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Wnon-virtual-dtor
        )
        if(GDPP_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
