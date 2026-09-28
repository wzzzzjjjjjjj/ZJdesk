cmake_minimum_required(VERSION 3.28)
project(ZJdesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ZJDESK_BUILD_CLIENT "Build the desktop client" ON)
option(ZJDESK_ENABLE_GSTREAMER "Link and inspect the installed GStreamer SDK" OFF)
option(ZJDESK_GUI_TESTS "Run GUI smoke tests on a visible desktop or Xvfb" OFF)
include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network WebSockets)
qt_standard_project_setup(REQUIRES 6.8)
configure_file(src/build_info.h.in generated/zjdesk/build_info.h @ONLY)
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(zjdesk_protocol STATIC src/protocol/message.cpp src/protocol/message.h)
target_include_directories(zjdesk_protocol PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(zjdesk_protocol PUBLIC Qt6::Core)

add_library(zjdesk_signaling STATIC src/signaling/room_server.cpp src/signaling/room_server.h)
target_link_libraries(zjdesk_signaling PUBLIC zjdesk_protocol Qt6::Network Qt6::WebSockets)

add_library(zjdesk_client_core STATIC
    src/signaling/signaling_client.cpp src/signaling/signaling_client.h
    src/session/session_coordinator.cpp src/session/session_coordinator.h
    src/remote-control/control_session.cpp src/remote-control/control_session.h
    src/diagnostics/event_log.cpp src/diagnostics/event_log.h
    src/media/media_environment.cpp src/media/media_environment.h
    src/platform/platform_info.cpp src/platform/platform_info.h)
target_link_libraries(zjdesk_client_core PUBLIC zjdesk_protocol Qt6::Network Qt6::WebSockets)
if(ZJDESK_ENABLE_GSTREAMER)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    find_package(GStreamer 1.26.11 REQUIRED)
    target_link_libraries(zjdesk_client_core PRIVATE GStreamer::GStreamer)
    target_compile_definitions(zjdesk_client_core PRIVATE ZJDESK_HAS_GSTREAMER=1)
endif()

qt_add_executable(zjdesk-server apps/signaling-server/main.cpp)
target_link_libraries(zjdesk-server PRIVATE zjdesk_signaling)
install(TARGETS zjdesk-server RUNTIME DESTINATION bin)

if(ZJDESK_BUILD_CLIENT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    qt_add_executable(zjdesk WIN32
        apps/client/main.cpp apps/client/main_window.cpp apps/client/main_window.h
        apps/client/video_surface.cpp apps/client/video_surface.h)
    qt_add_resources(zjdesk styles PREFIX "/" FILES resources/styles.qss)
    target_link_libraries(zjdesk PRIVATE zjdesk_client_core Qt6::Widgets Qt6::OpenGLWidgets)
    install(TARGETS zjdesk RUNTIME DESTINATION bin)
    qt_generate_deploy_app_script(TARGET zjdesk OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${deploy_script}")
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(zjdesk-unit-tests tests/unit/tst_core.cpp)
    target_link_libraries(zjdesk-unit-tests PRIVATE zjdesk_client_core Qt6::Test)
    qt_add_executable(zjdesk-integration-tests tests/integration/tst_signaling.cpp)
    target_link_libraries(zjdesk-integration-tests PRIVATE zjdesk_client_core zjdesk_signaling Qt6::Test)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-results")
    add_test(NAME core COMMAND zjdesk-unit-tests -o "${CMAKE_BINARY_DIR}/test-results/core.xml,junitxml")
    add_test(NAME signaling COMMAND zjdesk-integration-tests -o "${CMAKE_BINARY_DIR}/test-results/signaling.xml,junitxml")
    set_tests_properties(core signaling PROPERTIES TIMEOUT 30)
    if(ZJDESK_BUILD_CLIENT AND ZJDESK_GUI_TESTS)
        qt_add_executable(zjdesk-ui-tests tests/ui/tst_window.cpp
            apps/client/main_window.cpp apps/client/main_window.h
            apps/client/video_surface.cpp apps/client/video_surface.h)
        target_include_directories(zjdesk-ui-tests PRIVATE apps/client)
        target_link_libraries(zjdesk-ui-tests PRIVATE zjdesk_client_core zjdesk_signaling
            Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
        qt_add_resources(zjdesk-ui-tests test_styles PREFIX "/" FILES resources/styles.qss)
        add_test(NAME desktop COMMAND zjdesk-ui-tests -o "${CMAKE_BINARY_DIR}/test-results/desktop.xml,junitxml")
        set_tests_properties(desktop PROPERTIES TIMEOUT 30)
    endif()
endif()
