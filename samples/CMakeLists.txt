if(APPLE OR WIN32)
    add_subdirectory(demo)
endif()

if(SYSTEM_WEBVIEW_BUILD_GLFW_HOST AND (APPLE OR WIN32))
    add_subdirectory(glfw_demo)
endif()

if(SYSTEM_WEBVIEW_BUILD_SDL3_HOST AND (APPLE OR WIN32))
    add_subdirectory(sdl3_demo)
endif()

if(WIN32)
    add_subdirectory(windows_widget_demo)
endif()
