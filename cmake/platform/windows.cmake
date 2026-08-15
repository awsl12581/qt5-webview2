find_package(unofficial-webview2 CONFIG REQUIRED)

target_link_libraries(system_webview
    PRIVATE
        unofficial::webview2::webview2
        ole32
        shlwapi)
target_compile_definitions(system_webview PRIVATE SYSTEM_WEBVIEW_PLATFORM_WINDOWS=1)
