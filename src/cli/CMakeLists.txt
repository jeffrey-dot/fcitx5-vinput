option(VINPUT_FETCH_CLI11 "Fetch CLI11 with FetchContent when not found" ON)

find_package(CLI11 QUIET CONFIG)
if(NOT CLI11_FOUND)
    if(VINPUT_FETCH_CLI11)
        include(FetchContent)
        FetchContent_Declare(
            CLI11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11
            GIT_TAG v2.4.2
        )
        FetchContent_MakeAvailable(CLI11)
    else()
        message(FATAL_ERROR
            "CLI11 was not found. Install CLI11 or enable VINPUT_FETCH_CLI11.")
    endif()
endif()

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

add_executable(vinput
    main.cpp
    utils/formatter.cpp
    utils/editor_utils.cpp
    control/register.cpp
    control/register_daemon.cpp
    control/daemon_actions.cpp
    control/register_recording.cpp
    control/recording_actions.cpp
    config/asr_actions.cpp
    config/config_actions.cpp
    config/device_actions.cpp
    config/init_actions.cpp
    config/llm_actions.cpp
    config/register.cpp
    config/register_asr.cpp
    config/register_config.cpp
    config/register_device.cpp
    config/register_init.cpp
    config/register_llm.cpp
    config/register_scene.cpp
    config/scene_actions.cpp
    runtime/dbus_client.cpp
    runtime/systemd_client.cpp
    ${CMAKE_SOURCE_DIR}/src/common/pipewire_device.cpp
)

target_include_directories(vinput PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SDBUS_INCLUDE_DIRS}
)

target_link_libraries(vinput PRIVATE
    vinput-common
    CLI11::CLI11
    ${PIPEWIRE_LIBRARIES}
    ${SDBUS_LIBRARIES}
)

install(TARGETS vinput
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
