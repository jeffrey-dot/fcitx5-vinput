set(VINPUT_CONFIG_SOURCES
    config/core_config_defaults.cpp
    config/core_config_store.cpp
    config/core_config_normalize.cpp
    config/core_config_validate.cpp
    config/core_config_resolve.cpp
    config/core_config_json.cpp
    config/config_router.cpp
    config/vinput_config.cpp
    scene/postprocess_scene.cpp
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
    asr/recognition_result.cpp
    asr/model_manager.cpp
    registry/model_repository.cpp
    registry/cache.cpp
    registry/i18n.cpp
    registry/script_resource.cpp
    llm/adaptor_manager.cpp
    utils/download_progress.cpp
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
