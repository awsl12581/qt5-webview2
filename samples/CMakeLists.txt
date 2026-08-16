if(APPLE OR WIN32)
    add_subdirectory(demo)
endif()

if(WIN32)
    add_subdirectory(windows_widget_demo)
endif()
