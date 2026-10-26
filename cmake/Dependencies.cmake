function(system_webview_add_dependency source_dir binary_dir target_name)
    if(NOT TARGET "${target_name}")
        add_subdirectory("${source_dir}" "${binary_dir}" EXCLUDE_FROM_ALL)
    endif()
endfunction()

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install ON CACHE BOOL "" FORCE)
system_webview_add_dependency(
    "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/json"
    "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/json"
    nlohmann_json::nlohmann_json)

set(ADA_TESTING OFF CACHE BOOL "" FORCE)
set(ADA_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ADA_TOOLS OFF CACHE BOOL "" FORCE)
set(ADA_USE_SIMDUTF OFF CACHE BOOL "" FORCE)
set(ADA_INCLUDE_URL_PATTERN OFF CACHE BOOL "" FORCE)
set(ADA_BUILD_SINGLE_HEADER_LIB OFF CACHE BOOL "" FORCE)
system_webview_add_dependency(
    "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/ada"
    "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/ada"
    ada::ada)

function(system_webview_add_utf8proc)
    set(BUILD_SHARED_LIBS OFF)
    set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
    set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    system_webview_add_dependency(
        "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/utf8proc"
        "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/utf8proc"
        utf8proc::utf8proc)
endfunction()
system_webview_add_utf8proc()

function(system_webview_add_simdutf)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(SIMDUTF_TESTS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_TOOLS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(SIMDUTF_FUZZERS OFF CACHE BOOL "" FORCE)
    system_webview_add_dependency(
        "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/simdutf"
        "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/simdutf"
        simdutf::simdutf)
endfunction()
system_webview_add_simdutf()

if(SYSTEM_WEBVIEW_BUILD_QT_HOST OR SYSTEM_WEBVIEW_BUILD_QT_SAMPLE OR SYSTEM_WEBVIEW_BUILD_WINDOWS_WIDGET_SAMPLE)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Network)
endif()

if(SYSTEM_WEBVIEW_BUILD_GLFW_HOST OR SYSTEM_WEBVIEW_BUILD_GLFW_SAMPLE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    system_webview_add_dependency(
        "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/glfw3"
        "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/glfw3"
        glfw)
endif()

if(SYSTEM_WEBVIEW_BUILD_SDL3_HOST OR SYSTEM_WEBVIEW_BUILD_SDL3_SAMPLE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    system_webview_add_dependency(
        "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/sdl3"
        "${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/sdl3"
        SDL3::SDL3)
endif()
