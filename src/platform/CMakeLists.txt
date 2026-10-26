if(APPLE)
    set(SYSTEM_WEBVIEW_PLATFORM_SOURCES
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/PathSecurity.cpp
        PARENT_SCOPE)
elseif(WIN32)
    set(SYSTEM_WEBVIEW_PLATFORM_SOURCES
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/PathSecurity.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/WebView2Session.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/WebView2View.cpp
        PARENT_SCOPE)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()
