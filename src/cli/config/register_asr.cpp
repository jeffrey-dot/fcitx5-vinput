#include <memory>
#include <string>

#include <CLI/CLI.hpp>

#include "cli/config/action.h"
#include "cli/config/asr_actions.h"
#include "common/i18n.h"

namespace vinput::cli::config {

void RegisterAsrCommands(CLI::App &app, CliAction *action) {
  auto *asr = app.add_subcommand("asr", _("Manage ASR configuration"));
  asr->require_subcommand(1);

  auto *list = asr->add_subcommand("list", _("List configured ASR providers"));
  list->alias("ls");
  list->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigList(fmt, ctx);
    };
  });

  auto providerId = std::make_shared<std::string>();
  auto *remove = asr->add_subcommand("remove", _("Remove an ASR provider"));
  remove->alias("rm");
  remove->add_option("id", *providerId, _("Provider id"))->required();
  remove->callback([action, providerId]() {
    *action = [providerId](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigRemove(*providerId, fmt, ctx);
    };
  });

  auto activeId = std::make_shared<std::string>();
  auto *use = asr->add_subcommand("use", _("Set active ASR provider"));
  use->add_option("id", *activeId, _("Provider id"))->required();
  use->callback([action, activeId]() {
    *action = [activeId](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigUse(*activeId, fmt, ctx);
    };
  });

  auto *listModels = asr->add_subcommand("list-models", _("List ASR models"));
  listModels->alias("lsm");
  auto availableModels = std::make_shared<bool>(false);
  listModels->add_flag("-a,--available", *availableModels,
                       _("List available remote models"));
  listModels->callback([action, availableModels]() {
    *action = [availableModels](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigListModels(*availableModels, fmt, ctx);
    };
  });

  auto installModelSelector = std::make_shared<std::string>();
  auto *installModel =
      asr->add_subcommand("install", _("Install an ASR model"));
  installModel->add_option("id_or_index", *installModelSelector,
                           _("Model id or available-list index"))
      ->required();
  installModel->callback([action, installModelSelector]() {
    *action = [installModelSelector](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigInstallModel(*installModelSelector, fmt, ctx);
    };
  });

  auto modelSelector = std::make_shared<std::string>();
  auto *useModel = asr->add_subcommand("use-model", _("Set active local ASR model"));
  useModel->alias("um");
  useModel->add_option("id_or_index", *modelSelector,
                       _("Model id or installed-list index"))
      ->required();
  useModel->callback([action, modelSelector]() {
    *action = [modelSelector](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigUseModel(*modelSelector, fmt, ctx);
    };
  });

  auto infoModelSelector = std::make_shared<std::string>();
  auto *modelInfo = asr->add_subcommand("model-info", _("Show local model details"));
  modelInfo->alias("im");
  modelInfo->add_option("id_or_index", *infoModelSelector,
                        _("Model id or installed-list index"))
      ->required();
  modelInfo->callback([action, infoModelSelector]() {
    *action = [infoModelSelector](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigModelInfo(*infoModelSelector, fmt, ctx);
    };
  });

  auto *getHotword = asr->add_subcommand("get-hotword", _("Show configured hotwords file path"));
  getHotword->alias("gh");
  getHotword->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigGetHotword(fmt, ctx);
    };
  });

  auto hotwordPath = std::make_shared<std::string>();
  auto *setHotword = asr->add_subcommand("set-hotword", _("Set hotwords file path"));
  setHotword->alias("sh");
  setHotword->add_option("path", *hotwordPath, _("Path to hotwords file"))->required();
  setHotword->callback([action, hotwordPath]() {
    *action = [hotwordPath](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigSetHotword(*hotwordPath, fmt, ctx);
    };
  });

  auto *clearHotword = asr->add_subcommand("clear-hotword", _("Clear hotwords file path"));
  clearHotword->alias("ch");
  clearHotword->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigClearHotword(fmt, ctx);
    };
  });

  auto *editHotword = asr->add_subcommand("edit-hotword", _("Edit hotwords file in editor"));
  editHotword->alias("eh");
  editHotword->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigEditHotword(fmt, ctx);
    };
  });
}

}  // namespace vinput::cli::config
