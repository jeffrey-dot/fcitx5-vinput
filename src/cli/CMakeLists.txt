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
    config/asr_actions.cpp
    config/llm_actions.cpp
    config/register.cpp
    config/register_asr.cpp
    config/register_config.cpp
    config/register_device.cpp
    config/register_init.cpp
    config/register_llm.cpp
    config/register_scene.cpp
    command_init.cpp
    command_scene.cpp
    command_config.cpp
    command_device.cpp
    ${CMAKE_SOURCE_DIR}/src/common/pipewire_device.cpp
    ${CMAKE_SOURCE_DIR}/src/common/model_manager.cpp
)

target_include_directories(vinput PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PIPEWIRE_INCLUDE_DIRS}
)

target_link_libraries(vinput PRIVATE
    vinput-config
    CLI11::CLI11
    ${PIPEWIRE_LIBRARIES}
)

install(TARGETS vinput
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
