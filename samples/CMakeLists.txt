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
