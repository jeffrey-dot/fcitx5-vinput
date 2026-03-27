#include <memory>

#include <CLI/CLI.hpp>

#include "cli/config/action.h"
#include "cli/config/llm_actions.h"
#include "common/i18n.h"

namespace vinput::cli::config {

void RegisterLlmCommands(CLI::App &app, CliAction *action) {
  auto *llm = app.add_subcommand("llm", _("Manage LLM provider configuration"));
  llm->require_subcommand(1);

  auto *list = llm->add_subcommand("list", _("List configured LLM providers"));
  list->alias("ls");
  list->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunLlmConfigList(fmt, ctx);
    };
  });

  auto id = std::make_shared<std::string>();
  auto baseUrl = std::make_shared<std::string>();
  auto apiKey = std::make_shared<std::string>();
  auto *add = llm->add_subcommand("add", _("Add an LLM provider"));
  add->add_option("id", *id, _("Provider id"))->required();
  add->add_option("-u,--base-url", *baseUrl, _("Base URL"))->required();
  add->add_option("-k,--api-key", *apiKey, _("API key"));
  add->callback([action, id, baseUrl, apiKey]() {
    *action = [id, baseUrl, apiKey](Formatter &fmt, const CliContext &ctx) {
      return RunLlmConfigAdd(*id, *baseUrl, *apiKey, fmt, ctx);
    };
  });

  auto removeId = std::make_shared<std::string>();
  auto *remove = llm->add_subcommand("remove", _("Remove an LLM provider"));
  remove->alias("rm");
  remove->add_option("id", *removeId, _("Provider id"))->required();
  remove->callback([action, removeId]() {
    *action = [removeId](Formatter &fmt, const CliContext &ctx) {
      return RunLlmConfigRemove(*removeId, fmt, ctx);
    };
  });
}

}  // namespace vinput::cli::config
