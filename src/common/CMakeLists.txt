set(VINPUT_CONFIG_SOURCES
    config/core_config_defaults.cpp
    config/core_config_store.cpp
    config/core_config_normalize.cpp
    config/core_config_validate.cpp
    config/core_config_resolve.cpp
    config/core_config_json.cpp
    config/config_router.cpp
    config/vinput_config.cpp
    postprocess_scene.cpp
    i18n.cpp
    utils/path_utils.cpp
    utils/file_utils.cpp
)

add_library(vinput-config STATIC
    ${VINPUT_CONFIG_SOURCES}
)

add_library(vinput-common STATIC
    error_info.cpp
    ${VINPUT_CONFIG_SOURCES}
    recognition_result.cpp
    model_manager.cpp
    model_repository.cpp
    registry_cache.cpp
    registry_i18n.cpp
    script_resource.cpp
    adaptor_manager.cpp
    utils/downloader.cpp
    utils/process_utils.cpp
)

set_target_properties(vinput-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(vinput-config PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(vinput-common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(vinput-config PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vinput-common PUBLIC
    Fcitx5::Config
    nlohmann_json::nlohmann_json
    CURL::libcurl
    LibArchive::LibArchive
    OpenSSL::Crypto
    ${GETTEXT_LIBRARIES}
)

target_link_libraries(vinput-config PUBLIC
    Fcitx5::Config
    nlohmann_json::nlohmann_json
    ${GETTEXT_LIBRARIES}
)
