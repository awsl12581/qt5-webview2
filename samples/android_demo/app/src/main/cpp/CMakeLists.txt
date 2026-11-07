cmake_minimum_required(VERSION 3.24)
project(system_webview_android_demo LANGUAGES CXX)

# The demo only needs the portable core and the Android platform backend.
set(SYSTEM_WEBVIEW_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(SYSTEM_WEBVIEW_BUILD_GLFW_HOST OFF CACHE BOOL "" FORCE)
set(SYSTEM_WEBVIEW_BUILD_SDL3_HOST OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

set(SYSTEM_WEBVIEW_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../..")

add_subdirectory("${SYSTEM_WEBVIEW_REPO_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/system_webview")

add_library(system_webview_android_demo SHARED demo_bridge.cpp)
# log is needed by this library for __android_log_print; the JNI symbols come
# from the runtime and the core target exports its own include directory.
target_link_libraries(system_webview_android_demo PRIVATE system_webview log)
