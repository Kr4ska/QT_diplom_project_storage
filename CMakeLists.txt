cmake_minimum_required(VERSION 3.16)

project(diplom_project_storage VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Network QuickControls2)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appdiplom_project_storage
    main.cpp
)

qt_add_qml_module(appdiplom_project_storage
    URI diplom_project_storage
    QML_FILES
        Main.qml
        SOURCES
            databasemanager.h databasemanager.cpp
            layoutoptimizer.h layoutoptimizer.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appdiplom_project_storage PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appdiplom_project_storage
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appdiplom_project_storage
    PRIVATE
    Qt6::Quick
    Qt6::Sql
    Qt6::Network
)

install(TARGETS appdiplom_project_storage
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
