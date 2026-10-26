cmake_minimum_required(VERSION 3.24)
project(system_webview VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Keep local installs self-contained by default, while still allowing callers
# to provide the usual -DCMAKE_INSTALL_PREFIX override.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH
        "Install path prefix" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Network Widgets)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/json)
install(FILES 3rdparty/json/LICENSE.MIT
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/nlohmann_json")

set(ADA_TESTING OFF CACHE BOOL "" FORCE)
set(ADA_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ADA_TOOLS OFF CACHE BOOL "" FORCE)
set(ADA_USE_SIMDUTF OFF CACHE BOOL "" FORCE)
set(ADA_INCLUDE_URL_PATTERN OFF CACHE BOOL "" FORCE)
set(ADA_BUILD_SINGLE_HEADER_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/ada EXCLUDE_FROM_ALL)

function(system_webview_add_utf8proc)
    set(BUILD_SHARED_LIBS OFF)
    set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
    set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/utf8proc EXCLUDE_FROM_ALL)
endfunction()
system_webview_add_utf8proc()

function(system_webview_add_simdutf)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(SIMDUTF_TESTS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_TOOLS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_FUZZERS OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/simdutf EXCLUDE_FROM_ALL)
endfunction()
system_webview_add_simdutf()

add_library(system_webview SHARED
    include/system_webview/system_webview.h
    src/internal/HostView.h
    src/internal/HttpRange.cpp
    src/internal/HttpRange.h
    src/webview/TextEncoding.cpp
    src/internal/RandomToken.cpp
    src/webview/Url.cpp
    src/webview/Application.cpp
    src/webview/Diagnostics.cpp
    src/webview/WebViewBridge.cpp
    src/webview/WebResourceManager.cpp
    src/webview/JsonMessage.cpp
    src/webview/HostCompletion.cpp
    src/webview/ResourceMapping.cpp
    src/webview/WebViewPolicy.cpp
    src/webview/WebViewState.cpp
    src/webview/WebViewFactory.cpp
)
add_library(system_webview::system_webview ALIAS system_webview)
target_include_directories(system_webview PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(system_webview PUBLIC Qt5::Widgets nlohmann_json::nlohmann_json PRIVATE ada::ada utf8proc::utf8proc simdutf::simdutf)
target_compile_features(system_webview PUBLIC cxx_std_20)
target_compile_definitions(system_webview PRIVATE SYSTEM_WEBVIEW_BUILDING_LIBRARY=1)
set_target_properties(system_webview PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    target_sources(system_webview PRIVATE src/webview/PathSecurity.cpp)
    include(cmake/platform/macos.cmake)
elseif(WIN32)
    target_sources(system_webview PRIVATE
        src/platform/windows/PathSecurity.cpp
        src/platform/windows/WebView2Session.cpp
        src/platform/windows/WebView2View.cpp)
    include(cmake/platform/windows.cmake)
else()
    target_sources(system_webview PRIVATE src/webview/PathSecurity.cpp)
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

option(SYSTEM_WEBVIEW_BUILD_GLFW_HOST "Build the GLFW 3 host adapter target." OFF)
option(SYSTEM_WEBVIEW_BUILD_SDL3_HOST "Build the SDL 3 host adapter target." OFF)
add_library(system_webview_qt_host INTERFACE)
target_link_libraries(system_webview_qt_host INTERFACE system_webview Qt5::Widgets)
target_include_directories(system_webview_qt_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(SYSTEM_WEBVIEW_BUILD_GLFW_HOST)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/glfw3 EXCLUDE_FROM_ALL)
    add_library(system_webview_glfw_host INTERFACE)
    target_link_libraries(system_webview_glfw_host INTERFACE system_webview glfw)
    target_include_directories(system_webview_glfw_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if(SYSTEM_WEBVIEW_BUILD_SDL3_HOST)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/sdl3 EXCLUDE_FROM_ALL)
    add_library(system_webview_sdl3_host INTERFACE)
    target_link_libraries(system_webview_sdl3_host INTERFACE system_webview SDL3::SDL3)
    target_include_directories(system_webview_sdl3_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(TARGETS system_webview
    EXPORT system_webviewTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT system_webviewTargets
    FILE system_webviewTargets.cmake
    NAMESPACE system_webview::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/system_webview")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/system_webviewConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/system_webviewConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/system_webview")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/system_webviewConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/system_webviewConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/system_webviewConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/system_webview")

option(SYSTEM_WEBVIEW_BUILD_SAMPLES "Build the System WebView samples." ON)
if(SYSTEM_WEBVIEW_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(webview_core_tests
        tests/JsonMessageTests.cpp
        src/internal/RandomToken.cpp
        src/internal/HttpRange.cpp
        src/webview/Application.cpp
        src/webview/TextEncoding.cpp
        src/webview/Url.cpp
        src/webview/Diagnostics.cpp
        src/webview/WebViewBridge.cpp
        src/webview/WebResourceManager.cpp
        src/webview/JsonMessage.cpp
        src/webview/HostCompletion.cpp
        src/webview/ResourceMapping.cpp
        src/webview/WebViewPolicy.cpp
        src/webview/WebViewState.cpp)
    target_include_directories(webview_core_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(webview_core_tests PRIVATE SYSTEM_WEBVIEW_BUILDING_LIBRARY=1)
    target_link_libraries(webview_core_tests PRIVATE Qt5::Widgets nlohmann_json::nlohmann_json ada::ada utf8proc::utf8proc simdutf::simdutf)
    if(APPLE)
        target_link_libraries(webview_core_tests PRIVATE "${SECURITY_FRAMEWORK}")
    elseif(WIN32)
        target_link_libraries(webview_core_tests PRIVATE bcrypt)
    endif()
    add_test(NAME webview_core_tests COMMAND webview_core_tests)

    if(APPLE)
        target_sources(webview_core_tests PRIVATE src/webview/PathSecurity.cpp)
        add_executable(webview_macos_session_tests tests/WkSessionTests.mm)
        target_include_directories(webview_macos_session_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(webview_macos_session_tests PRIVATE system_webview)
        add_test(NAME webview_macos_session_tests COMMAND webview_macos_session_tests)

        add_executable(webview_macos_view_tests tests/WkViewTests.mm)
        target_include_directories(webview_macos_view_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(webview_macos_view_tests PRIVATE system_webview Qt5::Network)
        add_test(NAME webview_macos_view_tests COMMAND webview_macos_view_tests)
    elseif(WIN32)
        target_sources(webview_core_tests PRIVATE src/platform/windows/PathSecurity.cpp)
        add_executable(webview_windows_contract_tests tests/WebView2ContractTests.cpp src/webview/TextEncoding.cpp)
        target_include_directories(webview_windows_contract_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(webview_windows_contract_tests PRIVATE system_webview Qt5::Network simdutf::simdutf)
        add_test(NAME webview_windows_contract_tests COMMAND webview_windows_contract_tests)

        add_executable(webview_windows_runtime_probe tests/WebView2RuntimeProbe.cpp src/webview/TextEncoding.cpp)
        target_include_directories(webview_windows_runtime_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(webview_windows_runtime_probe PRIVATE system_webview Qt5::Network simdutf::simdutf)
        add_test(NAME webview_windows_runtime_probe COMMAND webview_windows_runtime_probe)
    endif()
endif()
