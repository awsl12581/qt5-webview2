set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/googletest
    ${SYSTEM_WEBVIEW_BINARY_DIR}/3rdparty/googletest EXCLUDE_FROM_ALL)

add_executable(webview_core_tests
    JsonMessageTests.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/internal/RandomToken.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/internal/HttpRange.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/Application.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/TextEncoding.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/Url.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/Diagnostics.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/WebViewBridge.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/WebResourceManager.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/JsonMessage.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/HostCompletion.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/ResourceMapping.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/WebViewPolicy.cpp
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/WebViewState.cpp)
target_include_directories(webview_core_tests PRIVATE
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/include
    ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
target_compile_definitions(webview_core_tests PRIVATE SYSTEM_WEBVIEW_BUILDING_LIBRARY=1)
target_compile_features(webview_core_tests PRIVATE cxx_std_20)
target_link_libraries(webview_core_tests PRIVATE
    gtest_main nlohmann_json::nlohmann_json ada::ada utf8proc::utf8proc simdutf::simdutf)
if(APPLE)
    target_sources(webview_core_tests PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/PathSecurity.cpp)
    target_link_libraries(webview_core_tests PRIVATE ${SECURITY_FRAMEWORK})
elseif(WIN32)
    target_sources(webview_core_tests PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/platform/windows/PathSecurity.cpp)
    target_link_libraries(webview_core_tests PRIVATE bcrypt)
endif()
add_test(NAME webview_core_tests COMMAND webview_core_tests)

if(APPLE)
    add_executable(webview_macos_session_tests WkSessionTests.mm)
    target_include_directories(webview_macos_session_tests PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
    target_link_libraries(webview_macos_session_tests PRIVATE
        system_webview gtest_main ${COCOA_FRAMEWORK})
    add_test(NAME webview_macos_session_tests COMMAND webview_macos_session_tests)

    add_executable(webview_macos_view_tests
        WkViewTests.mm
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/internal/RandomToken.cpp)
    target_include_directories(webview_macos_view_tests PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
    target_link_libraries(webview_macos_view_tests PRIVATE
        system_webview gtest_main ${COCOA_FRAMEWORK} ${SECURITY_FRAMEWORK})
    add_test(NAME webview_macos_view_tests COMMAND webview_macos_view_tests)
elseif(WIN32)
    add_executable(webview_windows_contract_tests
        WebView2ContractTests.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/TextEncoding.cpp)
    target_include_directories(webview_windows_contract_tests PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
    target_link_libraries(webview_windows_contract_tests PRIVATE
        system_webview gtest_main simdutf::simdutf)
    add_test(NAME webview_windows_contract_tests COMMAND webview_windows_contract_tests)

    add_executable(webview_windows_runtime_probe
        WebView2RuntimeProbe.cpp
        ${SYSTEM_WEBVIEW_SOURCE_DIR}/src/webview/TextEncoding.cpp)
    target_include_directories(webview_windows_runtime_probe PRIVATE ${SYSTEM_WEBVIEW_SOURCE_DIR}/src)
    target_link_libraries(webview_windows_runtime_probe PRIVATE
        system_webview gtest_main simdutf::simdutf)
    add_test(NAME webview_windows_runtime_probe COMMAND webview_windows_runtime_probe)
endif()
