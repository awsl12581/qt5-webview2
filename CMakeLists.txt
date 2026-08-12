cmake_minimum_required(VERSION 3.24)
project(system_webview LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# clangd looks for this file from the source tree. Point it at whichever preset
# was configured most recently without overwriting a user-owned regular file.
set(COMPILE_COMMANDS_LINK "${CMAKE_SOURCE_DIR}/compile_commands.json")
if(IS_SYMLINK "${COMPILE_COMMANDS_LINK}")
    file(REMOVE "${COMPILE_COMMANDS_LINK}")
endif()
if(NOT EXISTS "${COMPILE_COMMANDS_LINK}")
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json" "${COMPILE_COMMANDS_LINK}" SYMBOLIC)
elseif(NOT IS_SYMLINK "${COMPILE_COMMANDS_LINK}")
    message(WARNING "Not updating ${COMPILE_COMMANDS_LINK}: it is not a symbolic link.")
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)

add_library(system_webview STATIC
    src/webview/JsonMessage.cpp
    src/webview/WebViewPolicy.cpp
    src/webview/WebViewFactory.cpp
)
add_library(system_webview::system_webview ALIAS system_webview)
target_include_directories(system_webview PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(system_webview PUBLIC Qt5::Widgets)

if(APPLE)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)

    target_sources(system_webview PRIVATE
        src/platform/macos/WkWebView.mm
        src/platform/macos/WkWebViewSession.mm)
    target_link_libraries(system_webview PRIVATE
        "${WEBKIT_FRAMEWORK}" "${COCOA_FRAMEWORK}")
else()
    message(FATAL_ERROR "This initial project implements the macOS WKWebView backend only.")
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
        target_link_libraries(webview_macos_view_tests PRIVATE system_webview)
        add_test(NAME webview_macos_view_tests COMMAND webview_macos_view_tests)
    endif()
endif()
