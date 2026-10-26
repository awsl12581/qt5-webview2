cmake_minimum_required(VERSION 3.24)
project(system_webview VERSION 0.2.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)
include(cmake/HostAdapters.cmake)

if(SYSTEM_WEBVIEW_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
