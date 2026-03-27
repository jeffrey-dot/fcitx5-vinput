#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "common/config/core_config_types.h"

// API Functions
CoreConfig LoadCoreConfig();
bool LoadBundledDefaultCoreConfig(CoreConfig *config, std::string *error);
bool InitializeCoreConfig(std::string *error);
bool SaveCoreConfig(const CoreConfig &config);
std::string GetCoreConfigPath();

void to_json(nlohmann::ordered_json &j, const CoreConfig &config);
void from_json(const nlohmann::ordered_json &j, CoreConfig &config);

void NormalizeCoreConfig(CoreConfig *config);
bool ValidateCoreConfig(const CoreConfig &config, std::string *error);
const LlmProvider *ResolveLlmProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const LlmAdaptor *ResolveLlmAdaptor(const CoreConfig &config,
                                    const std::string &adaptor_id);
const AsrProvider *ResolveAsrProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const AsrProvider *ResolveActiveAsrProvider(const CoreConfig &config);
const AsrProvider *ResolveActiveLocalAsrProvider(const CoreConfig &config);
const AsrProvider *ResolvePreferredLocalAsrProvider(const CoreConfig &config);
std::string ResolveActiveLocalModel(const CoreConfig &config);
std::string ResolvePreferredLocalModel(const CoreConfig &config);
std::vector<std::string> ResolveModelRegistryUrls(const CoreConfig &config);
std::vector<std::string> ResolveAsrProviderRegistryUrls(const CoreConfig &config);
std::vector<std::string> ResolveLlmAdaptorRegistryUrls(const CoreConfig &config);
std::vector<std::string> ResolveRegistryI18nUrls(const CoreConfig &config,
                                                 const std::string &locale);
bool SetPreferredLocalModel(CoreConfig *config, const std::string &model,
                            std::string *error);
const vinput::scene::Definition *FindCommandScene(const CoreConfig &config);
std::filesystem::path ResolveModelBaseDir(const CoreConfig &config);
