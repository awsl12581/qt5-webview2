file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../demo/resources" SYSTEM_WEBVIEW_RESOURCE_DIR)

add_executable(system_webview_sdl3_demo main.cpp)
target_compile_definitions(system_webview_sdl3_demo PRIVATE
    SYSTEM_WEBVIEW_RESOURCE_DIR="${SYSTEM_WEBVIEW_RESOURCE_DIR}")
target_link_libraries(system_webview_sdl3_demo PRIVATE system_webview_sdl3_host)
system_webview_deploy_sample_runtime(system_webview_sdl3_demo)
