file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources" SYSTEM_WEBVIEW_RESOURCE_DIR)

add_executable(system_webview_demo
    main.cpp
    DemoWindow.cpp
)
target_include_directories(system_webview_demo PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(system_webview_demo PRIVATE
    SYSTEM_WEBVIEW_RESOURCE_DIR="${SYSTEM_WEBVIEW_RESOURCE_DIR}")
target_link_libraries(system_webview_demo PRIVATE system_webview::system_webview)
