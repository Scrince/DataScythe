find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 COMPONENTS Widgets REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(DataScythe
    main.cpp
    main_window.cpp
    main_window.h
    confirmation_dialog.cpp
    confirmation_dialog.h
    drive_table_model.cpp
    drive_table_model.h
    datascythe.qrc
)

target_link_libraries(DataScythe PRIVATE datascythe_core Qt${QT_VERSION_MAJOR}::Widgets)

if(APPLE)
    set_source_files_properties(resources/datascythe.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(DataScythe PRIVATE resources/datascythe.icns)
    set_target_properties(DataScythe PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE datascythe.icns
    )
endif()

if(WIN32)
    target_sources(DataScythe PRIVATE resources/datascythe.rc)
    target_link_libraries(DataScythe PRIVATE setupapi)
    set_target_properties(DataScythe PROPERTIES WIN32_EXECUTABLE TRUE)
    include(${CMAKE_SOURCE_DIR}/cmake/DeployWindows.cmake)
    datascythe_deploy_mingw(DataScythe)
    datascythe_deploy_qt(DataScythe)
endif()

install(TARGETS DataScythe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
