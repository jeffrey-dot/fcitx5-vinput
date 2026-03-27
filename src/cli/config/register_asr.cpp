#include <memory>

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

  auto *listModels = asr->add_subcommand("list-models", _("List local ASR models"));
  listModels->alias("lsm");
  listModels->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigListModels(fmt, ctx);
    };
  });

  auto modelId = std::make_shared<std::string>();
  auto *useModel = asr->add_subcommand("use-model", _("Set active local ASR model"));
  useModel->alias("um");
  useModel->add_option("id", *modelId, _("Model id"))->required();
  useModel->callback([action, modelId]() {
    *action = [modelId](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigUseModel(*modelId, fmt, ctx);
    };
  });

  auto infoModelId = std::make_shared<std::string>();
  auto *modelInfo = asr->add_subcommand("model-info", _("Show local model details"));
  modelInfo->alias("im");
  modelInfo->add_option("id", *infoModelId, _("Model id"))->required();
  modelInfo->callback([action, infoModelId]() {
    *action = [infoModelId](Formatter &fmt, const CliContext &ctx) {
      return RunAsrConfigModelInfo(*infoModelId, fmt, ctx);
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
