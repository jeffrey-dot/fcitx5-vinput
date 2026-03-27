#pragma once

#include "common/config/core_config.h"
#include "common/scene/postprocess_scene.h"
#include "common/asr/recognition_result.h"

#include <string>

class PostProcessor {
public:
  PostProcessor();
  ~PostProcessor();

  vinput::result::Payload Process(const std::string &raw_text,
                                  const vinput::scene::Definition &scene,
                                  const CoreConfig &settings,
                                  std::string *error_out = nullptr) const;

  vinput::result::Payload ProcessCommand(const std::string &asr_text,
                                         const std::string &selected_text,
                                         const vinput::scene::Definition &command_scene,
                                         const CoreConfig &settings,
                                         std::string *error_out = nullptr) const;
};
