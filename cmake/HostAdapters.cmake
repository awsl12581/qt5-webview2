function(system_webview_add_host_adapter name dependency)
    add_library("${name}" INTERFACE)
    target_link_libraries("${name}" INTERFACE system_webview "${dependency}")
    target_include_directories("${name}" INTERFACE
        "$<BUILD_INTERFACE:${SYSTEM_WEBVIEW_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
endfunction()

if(SYSTEM_WEBVIEW_BUILD_QT_HOST)
    system_webview_add_host_adapter(system_webview_qt_host Qt5::Widgets)
endif()

if(SYSTEM_WEBVIEW_BUILD_GLFW_HOST)
    system_webview_add_host_adapter(system_webview_glfw_host glfw)
endif()

if(SYSTEM_WEBVIEW_BUILD_SDL3_HOST)
    system_webview_add_host_adapter(system_webview_sdl3_host SDL3::SDL3)
endif()
