cmake_minimum_required(VERSION 3.28)
project(ZJdesk VERSION 0.4.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ZJDESK_BUILD_CLIENT "Build the desktop client" ON)
option(ZJDESK_ENABLE_GSTREAMER "Enable GStreamer preview and screen sharing" OFF)
option(ZJDESK_GUI_TESTS "Run GUI smoke tests on a visible desktop or Xvfb" OFF)
include(CTest)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network WebSockets)
qt_standard_project_setup(REQUIRES 6.8)
configure_file(src/build_info.h.in generated/zjdesk/build_info.h @ONLY)
if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(zjdesk_protocol STATIC src/protocol/message.cpp src/protocol/message.h
    src/protocol/annotation.cpp src/protocol/annotation.h)
target_include_directories(zjdesk_protocol PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(zjdesk_protocol PUBLIC Qt6::Core)

add_library(zjdesk_signaling STATIC src/signaling/room_server.cpp src/signaling/room_server.h)
target_link_libraries(zjdesk_signaling PUBLIC zjdesk_protocol Qt6::Network Qt6::WebSockets)

add_library(zjdesk_client_core STATIC
    src/signaling/signaling_client.cpp src/signaling/signaling_client.h
    src/session/session_coordinator.cpp src/session/session_coordinator.h
    src/session/meeting_share.cpp
    src/session/network_setup.cpp src/session/network_setup.h
    src/remote-control/control_session.cpp src/remote-control/control_session.h
    src/remote-control/file_clipboard.cpp src/remote-control/file_clipboard.h
    src/remote-control/input_injector.cpp src/remote-control/input_injector.h
    src/remote-control/input_receiver.cpp src/remote-control/input_receiver.h
    src/diagnostics/event_log.cpp src/diagnostics/event_log.h
    src/media/media_environment.cpp src/media/media_environment.h
    src/media/media_engine.cpp src/media/media_engine.h
    src/media/conference_media.cpp src/media/conference_media.h
    src/media/camera_mode.cpp src/media/camera_mode.h
    src/media/screen_share.cpp src/media/screen_share.h
    src/media/share_quality.cpp src/media/share_quality.h
    src/media/frame_timing.h
    src/media/congestion_budget.h
    src/platform/platform_info.cpp src/platform/platform_info.h
    src/platform/camera_permission.cpp src/platform/camera_permission.h src/platform/camera_permission_p.h)
target_link_libraries(zjdesk_client_core PUBLIC zjdesk_protocol zjdesk_signaling Qt6::Gui Qt6::Network Qt6::WebSockets)
if(WIN32)
    target_link_libraries(zjdesk_client_core PRIVATE windowsapp user32 gdi32)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    if(NOT TARGET X11::Xtst OR NOT TARGET X11::Xfixes)
        message(FATAL_ERROR "Install libxtst-dev and libxfixes-dev for Linux remote control")
    endif()
    target_link_libraries(zjdesk_client_core PRIVATE X11::X11 X11::Xtst X11::Xfixes)
    target_compile_definitions(zjdesk_client_core PRIVATE ZJDESK_HAS_XTEST=1)
endif()
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
        apps/client/annotation_layer.cpp apps/client/annotation_layer.h
        apps/client/video_surface.cpp apps/client/video_surface.h)
    qt_add_resources(zjdesk styles PREFIX "/" FILES resources/styles.qss)
    qt_add_resources(zjdesk branding PREFIX "/icons" BASE packaging FILES packaging/zjdesk.png)
    if(WIN32)
        enable_language(RC)
        configure_file(packaging/zjdesk.rc.in generated/zjdesk.rc @ONLY)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/zjdesk.rc" PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/zjdesk.ico")
        target_sources(zjdesk PRIVATE "${CMAKE_BINARY_DIR}/generated/zjdesk.rc")
    endif()
    target_link_libraries(zjdesk PRIVATE zjdesk_client_core Qt6::Widgets Qt6::OpenGLWidgets)
    install(TARGETS zjdesk RUNTIME DESTINATION bin)
    if(WIN32)
        install(FILES tools/ip-info.ps1 tools/ip-info.cmd DESTINATION bin/tools)
    else()
        install(PROGRAMS tools/ip-info.sh DESTINATION bin/tools)
    endif()
    add_custom_command(TARGET zjdesk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/tools" "$<TARGET_FILE_DIR:zjdesk>/tools")
    qt_generate_deploy_app_script(TARGET zjdesk OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${deploy_script}")
    if(WIN32 AND ZJDESK_ENABLE_GSTREAMER)
        install(DIRECTORY "${GST_ROOT}/bin/" DESTINATION bin FILES_MATCHING PATTERN "*.dll")
        install(DIRECTORY "${GST_ROOT}/lib/gstreamer-1.0/"
            DESTINATION bin/gstreamer/lib/gstreamer-1.0 FILES_MATCHING PATTERN "*.dll")
        install(PROGRAMS "${GST_ROOT}/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
            DESTINATION bin/gstreamer/libexec/gstreamer-1.0)
        install(DIRECTORY "${GST_ROOT}/share/licenses/" DESTINATION licenses/gstreamer)
        install(FILES "${GST_ROOT}/share/versions.txt" DESTINATION licenses/gstreamer)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(zjdesk-unit-tests tests/unit/tst_core.cpp)
    qt_add_executable(zjdesk-file-tests tests/unit/tst_file_clipboard.cpp)
    target_link_libraries(zjdesk-file-tests PRIVATE zjdesk_client_core Qt6::Test)
    target_link_libraries(zjdesk-unit-tests PRIVATE zjdesk_client_core Qt6::Test)
    qt_add_executable(zjdesk-integration-tests tests/integration/tst_signaling.cpp)
    add_executable(zjdesk-network-ping-stub tests/unit/network_ping_stub.cpp)
    qt_add_executable(zjdesk-network-tests tests/unit/tst_network_setup.cpp)
    target_link_libraries(zjdesk-network-tests PRIVATE zjdesk_client_core Qt6::Test)
    add_dependencies(zjdesk-network-tests zjdesk-network-ping-stub)
    target_link_libraries(zjdesk-integration-tests PRIVATE zjdesk_client_core zjdesk_signaling Qt6::Test)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-results")
    add_test(NAME core COMMAND zjdesk-unit-tests -o "${CMAKE_BINARY_DIR}/test-results/core.xml,junitxml")
    add_test(NAME file-clipboard COMMAND zjdesk-file-tests -o "${CMAKE_BINARY_DIR}/test-results/file-clipboard.xml,junitxml")
    add_test(NAME signaling COMMAND zjdesk-integration-tests -o "${CMAKE_BINARY_DIR}/test-results/signaling.xml,junitxml")
    add_test(NAME network-setup COMMAND zjdesk-network-tests -o "${CMAKE_BINARY_DIR}/test-results/network-setup.xml,junitxml")
    set_tests_properties(network-setup PROPERTIES TIMEOUT 30)
    set_tests_properties(core signaling file-clipboard PROPERTIES TIMEOUT 30)
    if(ZJDESK_ENABLE_GSTREAMER)
        qt_add_executable(zjdesk-media-tests tests/integration/tst_media.cpp)
        target_link_libraries(zjdesk-media-tests PRIVATE zjdesk_client_core zjdesk_signaling Qt6::Test)
        qt_add_executable(zjdesk-meeting-probe tests/manual/meeting_probe.cpp)
        target_link_libraries(zjdesk-meeting-probe PRIVATE zjdesk_client_core)
        add_test(NAME media COMMAND zjdesk-media-tests -o "${CMAKE_BINARY_DIR}/test-results/media.xml,junitxml")
        set_tests_properties(media PROPERTIES TIMEOUT 180)
    endif()
    if(ZJDESK_BUILD_CLIENT AND ZJDESK_GUI_TESTS)
        qt_add_executable(zjdesk-ui-tests tests/ui/tst_window.cpp
            apps/client/main_window.cpp apps/client/main_window.h
            apps/client/annotation_layer.cpp apps/client/annotation_layer.h
            apps/client/video_surface.cpp apps/client/video_surface.h)
        target_include_directories(zjdesk-ui-tests PRIVATE apps/client)
        target_link_libraries(zjdesk-ui-tests PRIVATE zjdesk_client_core zjdesk_signaling
            Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
        add_dependencies(zjdesk-ui-tests zjdesk-network-ping-stub)
        qt_add_resources(zjdesk-ui-tests test_styles PREFIX "/" FILES resources/styles.qss)
        qt_add_resources(zjdesk-ui-tests test_branding PREFIX "/icons" BASE packaging FILES packaging/zjdesk.png)
        add_test(NAME desktop COMMAND zjdesk-ui-tests -o "${CMAKE_BINARY_DIR}/test-results/desktop.xml,junitxml")
        set_tests_properties(desktop PROPERTIES TIMEOUT 90)
    endif()
endif()

# A low-integrity workspace label otherwise makes its desktop executables run
# at low integrity, where Windows denies camera access despite enabled consent.
if(WIN32)
    find_program(ZJDESK_POWERSHELL NAMES powershell REQUIRED)
    foreach(runtime_target zjdesk zjdesk-unit-tests zjdesk-file-tests zjdesk-integration-tests zjdesk-network-tests zjdesk-network-ping-stub zjdesk-media-tests zjdesk-ui-tests zjdesk-meeting-probe)
        if(TARGET ${runtime_target})
            add_custom_command(TARGET ${runtime_target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E env --unset=PSModulePath
                    "${ZJDESK_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SetWindowsRuntimeIntegrity.ps1"
                    -Executable "$<TARGET_FILE:${runtime_target}>"
                VERBATIM)
        endif()
    endforeach()
    if(TARGET zjdesk)
        install(CODE "execute_process(
            COMMAND \"${CMAKE_COMMAND}\" -E env --unset=PSModulePath
                \"${ZJDESK_POWERSHELL}\" -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/SetWindowsRuntimeIntegrity.ps1\"
                -Executable \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/zjdesk.exe\"
            COMMAND_ERROR_IS_FATAL ANY)")
    endif()
    if(BUILD_TESTING)
        add_test(NAME windows-runtime-integrity
            COMMAND "${CMAKE_COMMAND}" -E env --unset=PSModulePath
                "${ZJDESK_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/test_windows_runtime_integrity.ps1"
                -WorkRoot "${CMAKE_BINARY_DIR}")
        set_tests_properties(windows-runtime-integrity PROPERTIES TIMEOUT 30)
    endif()
endif()
