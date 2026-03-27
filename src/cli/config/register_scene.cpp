#include <memory>

#include <CLI/CLI.hpp>

#include "cli/command_scene.h"
#include "cli/config/action.h"
#include "common/i18n.h"
#include "common/postprocess_scene.h"

namespace vinput::cli::config {

void RegisterSceneCommands(CLI::App &app, CliAction *action) {
  auto *scene = app.add_subcommand("scene", _("Manage recognition scenes"));
  scene->require_subcommand(1);

  auto *list = scene->add_subcommand("list", _("List all scenes"));
  list->alias("ls");
  list->callback([action]() {
    *action = [](Formatter &fmt, const CliContext &ctx) {
      return RunSceneList(fmt, ctx);
    };
  });

  struct AddState {
    std::string id;
    std::string label;
    std::string prompt;
    std::string providerId;
    std::string model;
    int candidates = vinput::scene::kDefaultCandidateCount;
    int timeoutMs = vinput::scene::kDefaultTimeoutMs;
  };
  auto addState = std::make_shared<AddState>();
  auto *add = scene->add_subcommand("add", _("Add a new scene"));
  add->add_option("--id", addState->id, _("Scene id"))->required();
  add->add_option("-l,--label", addState->label, _("Display label"));
  add->add_option("-t,--prompt", addState->prompt, _("LLM prompt"));
  add->add_option("-p,--provider", addState->providerId, _("LLM provider id"));
  add->add_option("-m,--model", addState->model, _("LLM model id"));
  add->add_option("-c,--candidates", addState->candidates, _("Candidate count"))
      ->default_val(vinput::scene::kDefaultCandidateCount);
  add->add_option("--timeout", addState->timeoutMs,
                  _("Request timeout in milliseconds"))
      ->default_val(vinput::scene::kDefaultTimeoutMs);
  add->callback([action, addState]() {
    *action = [addState](Formatter &fmt, const CliContext &ctx) {
      return RunSceneAdd(addState->id, addState->label, addState->prompt,
                         addState->providerId, addState->model,
                         addState->candidates, addState->timeoutMs, fmt, ctx);
    };
  });

  auto useId = std::make_shared<std::string>();
  auto *use = scene->add_subcommand("use", _("Set active scene"));
  use->add_option("id", *useId, _("Scene id"))->required();
  use->callback([action, useId]() {
    *action = [useId](Formatter &fmt, const CliContext &ctx) {
      return RunSceneUse(*useId, fmt, ctx);
    };
  });

  auto removeId = std::make_shared<std::string>();
  auto *remove = scene->add_subcommand("remove", _("Remove a scene"));
  remove->alias("rm");
  remove->add_option("id", *removeId, _("Scene id"))->required();
  remove->callback([action, removeId]() {
    *action = [removeId](Formatter &fmt, const CliContext &ctx) {
      return RunSceneRemove(*removeId, false, fmt, ctx);
    };
  });
}

}  // namespace vinput::cli::config
