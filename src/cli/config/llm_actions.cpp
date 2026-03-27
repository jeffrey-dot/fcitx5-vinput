#include "cli/config/llm_actions.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "cli/utils/cli_helpers.h"
#include "common/config/core_config.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"

namespace {

std::string MaskApiKey(const std::string &key) {
  if (key.size() <= 8) {
    return std::string(key.size(), '*');
  }
  return key.substr(0, 4) + std::string(key.size() - 8, '*') +
         key.substr(key.size() - 4);
}

}  // namespace

int RunLlmConfigList(Formatter &fmt, const CliContext &ctx) {
  CoreConfig config = LoadCoreConfig();

  if (ctx.json_output) {
    nlohmann::json providers = nlohmann::json::array();
    for (const auto &provider : config.llm.providers) {
      providers.push_back({
          {"id", provider.id},
          {"base_url", provider.base_url},
          {"api_key", ""},
      });
    }
    fmt.PrintJson(providers);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("BASE_URL"), _("API_KEY")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &provider : config.llm.providers) {
    rows.push_back({provider.id, provider.base_url, MaskApiKey(provider.api_key)});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunLlmConfigAdd(const std::string &id, const std::string &baseUrl,
                    const std::string &apiKey, Formatter &fmt,
                    const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  if (ResolveLlmProvider(config, id) != nullptr) {
    fmt.PrintError(vinput::str::FmtStr(_("LLM provider '%s' already exists."), id));
    return 1;
  }

  LlmProvider provider;
  provider.id = id;
  provider.base_url = baseUrl;
  provider.api_key = apiKey;
  config.llm.providers.push_back(std::move(provider));

  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("LLM provider '%s' added."), id));
  return 0;
}

int RunLlmConfigRemove(const std::string &id, Formatter &fmt,
                       const CliContext &ctx) {
  (void)ctx;
  CoreConfig config = LoadCoreConfig();
  auto &providers = config.llm.providers;
  const auto it = std::find_if(
      providers.begin(), providers.end(),
      [&id](const LlmProvider &provider) { return provider.id == id; });
  if (it == providers.end()) {
    fmt.PrintError(vinput::str::FmtStr(_("LLM provider '%s' not found."), id));
    return 1;
  }

  providers.erase(it);
  if (!SaveConfigOrFail(config, fmt)) {
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("LLM provider '%s' removed."), id));
  return 0;
}
