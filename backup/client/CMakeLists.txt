cmake_minimum_required(VERSION 3.16)

project(chat VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    registerpage.cpp
    registerpage.h
    registerpage.ui
    forgotpasswordpage.cpp
    forgotpasswordpage.h
    logicdialog.cpp
    logicdialog.h
    logicdialog.ui
    httpmgr.cpp
    httpmgr.h
    singleton.h
    const.h
    resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(chat
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        assets/1.png assets/2.png assets/3.png assets/4.png assets/avatar.png
    )
else()
    if(ANDROID)
        add_library(chat SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(chat
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(chat PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.chat)
endif()

set_target_properties(chat PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS chat
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(CHAT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")

if(WIN32 AND EXISTS "${CHAT_CONFIG_FILE}")
    add_custom_command(TARGET chat POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CHAT_CONFIG_FILE}"
            "$<TARGET_FILE_DIR:chat>/config.ini"
        COMMENT "Copying config.ini to output directory"
    )
elseif(WIN32)
    message(WARNING
        "config.ini was not found. Copy config.ini.example to config.ini before running the client."
    )
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(chat)
endif()
