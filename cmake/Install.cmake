install(DIRECTORY "${SYSTEM_WEBVIEW_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${SYSTEM_WEBVIEW_SOURCE_DIR}/3rdparty/json/LICENSE.MIT"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/nlohmann_json")
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
    "${SYSTEM_WEBVIEW_SOURCE_DIR}/cmake/system_webviewConfig.cmake.in"
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
