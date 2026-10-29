function(system_webview_deploy_sample_runtime target)
    if(WIN32)
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS $<TARGET_RUNTIME_DLLS:${target}>)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
endfunction()

if(SYSTEM_WEBVIEW_BUILD_QT_SAMPLE AND (APPLE OR WIN32))
    add_subdirectory(demo)
endif()

if(SYSTEM_WEBVIEW_BUILD_GLFW_SAMPLE AND (APPLE OR WIN32))
    add_subdirectory(glfw_demo)
endif()

if(SYSTEM_WEBVIEW_BUILD_SDL3_SAMPLE AND (APPLE OR WIN32))
    add_subdirectory(sdl3_demo)
endif()

if(SYSTEM_WEBVIEW_BUILD_WINDOWS_WIDGET_SAMPLE AND WIN32)
    add_subdirectory(windows_widget_demo)
endif()
