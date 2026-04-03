function(cute_xmpp_set_project_warnings target warnings_as_errors)
    if(MSVC)
        set(project_warnings
            /W4
            /permissive-
            /Zc:__cplusplus
        )
        if(warnings_as_errors)
            list(APPEND project_warnings /WX)
        endif()
    else()
        set(project_warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
        if(warnings_as_errors)
            list(APPEND project_warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${project_warnings})
endfunction()
