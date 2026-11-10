set(SYSTEM_WEBVIEW_PLATFORM_SOURCES)

if(APPLE)
    set(SYSTEM_WEBVIEW_PLATFORM_SOURCES
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/macos/PathSecurity.cpp)
elseif(WIN32)
    set(SYSTEM_WEBVIEW_PLATFORM_SOURCES
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/PathSecurity.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/WebView2Session.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/WebView2View.cpp)
elseif(ANDROID)
    set(SYSTEM_WEBVIEW_PLATFORM_SOURCES
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/android/PathSecurity.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/android/AndroidJni.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/android/AndroidSession.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/android/AndroidView.cpp)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

set(SYSTEM_WEBVIEW_PLATFORM_SOURCES ${SYSTEM_WEBVIEW_PLATFORM_SOURCES} PARENT_SCOPE)
