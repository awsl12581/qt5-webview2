cmake_minimum_required(VERSION 3.24)
project(system_webview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Network Widgets)

add_library(system_webview STATIC
    src/webview/JsonMessage.cpp
    src/webview/HostCompletion.cpp
    src/webview/ResourceMapping.cpp
    src/webview/WebViewPolicy.cpp
    src/webview/WebViewState.cpp
)
add_library(system_webview::system_webview ALIAS system_webview)
target_include_directories(system_webview PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(system_webview PUBLIC Qt5::Widgets)

if(APPLE)
    include(cmake/platform/macos.cmake)
elseif(WIN32)
    include(cmake/platform/windows.cmake)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

option(SYSTEM_WEBVIEW_BUILD_SAMPLES "Build the System WebView samples." ON)
if(SYSTEM_WEBVIEW_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(webview_core_tests tests/JsonMessageTests.cpp)
    target_link_libraries(webview_core_tests PRIVATE system_webview)
    add_test(NAME webview_core_tests COMMAND webview_core_tests)

    if(APPLE)
        add_executable(webview_macos_session_tests tests/WkSessionTests.mm)
        target_link_libraries(webview_macos_session_tests PRIVATE system_webview)
        add_test(NAME webview_macos_session_tests COMMAND webview_macos_session_tests)

        add_executable(webview_macos_view_tests tests/WkViewTests.mm)
        target_link_libraries(webview_macos_view_tests PRIVATE system_webview Qt5::Network)
        add_test(NAME webview_macos_view_tests COMMAND webview_macos_view_tests)
    endif()
endif()
