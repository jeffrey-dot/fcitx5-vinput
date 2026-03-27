#pragma once

#include "common/config/core_config.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vinput::asr {

constexpr std::size_t kMinSamplesForInference = 8000;

struct Result {
  bool ok = true;
  std::string text;
  std::string error;
};

class Provider {
public:
  virtual ~Provider() = default;

  virtual bool Init(const CoreConfig &config, std::string *error) = 0;
  virtual Result Infer(const std::vector<int16_t> &pcm_data) = 0;
  virtual void Shutdown() = 0;
};

std::unique_ptr<Provider> CreateProvider(const CoreConfig &config,
                                         std::string *error);

}  // namespace vinput::asr
