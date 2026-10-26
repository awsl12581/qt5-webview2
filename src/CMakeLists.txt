add_subdirectory(internal)
add_subdirectory(webview)
add_subdirectory(platform)

add_library(system_webview SHARED
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/include/system_webview/system_webview.h
    ${SYSTEM_WEBVIEW_INTERNAL_SOURCES}
    ${SYSTEM_WEBVIEW_WEBVIEW_SOURCES}
    ${SYSTEM_WEBVIEW_PLATFORM_SOURCES})
add_library(system_webview::system_webview ALIAS system_webview)

target_include_directories(system_webview PUBLIC
    $<BUILD_INTERFACE:${SYSTEM_WEBVIEW_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
target_link_libraries(system_webview PUBLIC
    nlohmann_json::nlohmann_json
    PRIVATE ada::ada utf8proc::utf8proc simdutf::simdutf)
target_compile_features(system_webview PUBLIC cxx_std_20)
target_compile_definitions(system_webview PRIVATE SYSTEM_WEBVIEW_BUILDING_LIBRARY=1)
set_target_properties(system_webview PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    OBJCXX_STANDARD 20
    OBJCXX_STANDARD_REQUIRED YES
    OBJCXX_EXTENSIONS NO
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    include(${SYSTEM_WEBVIEW_SOURCE_DIR}/cmake/platform/macos.cmake)
elseif(WIN32)
    include(${SYSTEM_WEBVIEW_SOURCE_DIR}/cmake/platform/windows.cmake)
endif()
