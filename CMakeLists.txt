cmake_minimum_required(VERSION 3.24)

project(CuteXMPP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CUTE_XMPP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CUTE_XMPP_ENABLE_LTO "Enable link-time optimization" OFF)

include(cmake/CompilerWarnings.cmake)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(QXmppQt6 REQUIRED)

qt_standard_project_setup()

qt_add_executable(CuteXMPP
    WIN32
    MACOSX_BUNDLE
    resources/resources.qrc
    src/main.cpp
    src/models/types.h
    src/logging/app_logger.cpp
    src/logging/app_logger.h
    src/settings/app_settings.cpp
    src/settings/app_settings.h
    src/settings/settings_store.cpp
    src/settings/settings_store.h
    src/ui/auth_dialog.cpp
    src/ui/auth_dialog.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/settings_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/theme.cpp
    src/ui/theme.h
    src/ui/widgets.cpp
    src/ui/widgets.h
    src/xmpp/xmpp_service.cpp
    src/xmpp/xmpp_service.h
)

target_include_directories(CuteXMPP PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(CuteXMPP PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets QXmpp::QXmpp)

if(WIN32)
    target_compile_definitions(CuteXMPP PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6 HINTS "C:/msys64/ucrt64/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET CuteXMPP POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-compiler-runtime "$<TARGET_FILE:CuteXMPP>"
            COMMENT "Deploying Qt runtime with windeployqt"
        )
    endif()
endif()

cute_xmpp_set_project_warnings(CuteXMPP ${CUTE_XMPP_WARNINGS_AS_ERRORS})

if(CUTE_XMPP_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET CuteXMPP PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO/LTO is not available: ${ipo_error}")
    endif()
endif()

set_target_properties(CuteXMPP PROPERTIES
    OUTPUT_NAME "CuteXMPP"
    MACOSX_BUNDLE_BUNDLE_NAME "CuteXMPP"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.cutexmpp.client"
)

install(TARGETS CuteXMPP
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
