#include "daemon/asr/backends/vosk_offline_backend.h"

#include "common/asr/model_manager.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"
#include "daemon/asr/asr_engine.h"

#include <nlohmann/json.hpp>
#include <vosk_api.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

namespace vinput::daemon::asr {

namespace {

std::string JsonStringField(const char *raw_json, std::string_view key) {
  if (!raw_json || *raw_json == '\0') {
    return {};
  }

  try {
    const auto parsed = nlohmann::json::parse(raw_json);
    if (!parsed.is_object() || !parsed.contains(key) || !parsed[key].is_string()) {
      return {};
    }
    return vinput::str::TrimAsciiWhitespace(parsed[key].get<std::string>());
  } catch (...) {
    return {};
  }
}

int SafeStoi(const std::string &value, int fallback) {
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

int ResolveSampleRate(const ModelInfo &model_info) {
  if (model_info.recognizer_config.contains("feat_config") &&
      model_info.recognizer_config["feat_config"].is_object()) {
    const auto &feat_config = model_info.recognizer_config["feat_config"];
    if (feat_config.contains("sample_rate")) {
      const auto &value = feat_config["sample_rate"];
      if (value.is_number_integer()) {
        return value.get<int>();
      }
      if (value.is_string()) {
        return SafeStoi(value.get<std::string>(), 16000);
      }
    }
  }
  return SafeStoi(model_info.Param("sample_rate", "16000"), 16000);
}

bool ShouldConcatenateWithoutSpace(std::string_view language) {
  return language == "zh" || language == "zh_cn" || language == "zh-hans" ||
         language == "zh-hant" || language == "yue" || language == "ja" ||
         language == "th" || language == "lo" || language == "my" ||
         language == "km" || language == "bo";
}

std::string NormalizeRecognizedText(std::string text,
                                    bool concatenate_without_space) {
  text = vinput::str::TrimAsciiWhitespace(text);
  if (!concatenate_without_space || text.empty()) {
    return text;
  }

  std::string normalized;
  normalized.reserve(text.size());
  for (char ch : text) {
    if (!std::isspace(static_cast<unsigned char>(ch))) {
      normalized.push_back(ch);
    }
  }
  return normalized;
}

class VoskOfflineSession : public RecognitionSession {
public:
  VoskOfflineSession(VoskModel *model, int sample_rate, float input_gain,
                     bool concatenate_without_space)
      : model_(model),
        sample_rate_(sample_rate),
        input_gain_(input_gain),
        concatenate_without_space_(concatenate_without_space) {}

  bool PushAudio(std::span<const int16_t> pcm, std::string *error) override {
    if (finished_) {
      if (error) {
        *error = "Recognition session already finished.";
      }
      return false;
    }
    pcm_.insert(pcm_.end(), pcm.begin(), pcm.end());
    if (error) {
      error->clear();
    }
    return true;
  }

  bool Finish(std::string *error) override {
    if (finished_) {
      if (error) {
        error->clear();
      }
      return true;
    }

    finished_ = true;
    if (!model_) {
      if (error) {
        *error = "Vosk model is not initialized.";
      }
      events_.push_back({RecognitionEventKind::Error, {}, error ? *error : ""});
      events_.push_back({RecognitionEventKind::Completed, {}, {}});
      return false;
    }

    VoskRecognizer *recognizer =
        vosk_recognizer_new(model_, static_cast<float>(sample_rate_));
    if (!recognizer) {
      if (error) {
        *error = "failed to create vosk recognizer";
      }
      events_.push_back({RecognitionEventKind::Error, {}, error ? *error : ""});
      events_.push_back({RecognitionEventKind::Completed, {}, {}});
      return false;
    }

    vosk_recognizer_set_words(recognizer, 0);
    vosk_recognizer_set_partial_words(recognizer, 0);
    vosk_recognizer_set_max_alternatives(recognizer, 0);

    std::vector<short> samples(pcm_.begin(), pcm_.end());
    if (input_gain_ != 1.0f) {
      for (auto &sample : samples) {
        const float scaled = static_cast<float>(sample) * input_gain_;
        const float clamped = std::clamp(scaled, -32768.0f, 32767.0f);
        sample = static_cast<short>(clamped);
      }
    }

    const int accepted = vosk_recognizer_accept_waveform_s(
        recognizer, samples.data(), static_cast<int>(samples.size()));
    if (accepted < 0) {
      if (error) {
        *error = "vosk failed to accept audio";
      }
      events_.push_back({RecognitionEventKind::Error, {}, error ? *error : ""});
      vosk_recognizer_free(recognizer);
      events_.push_back({RecognitionEventKind::Completed, {}, {}});
      return false;
    }

    std::string text = JsonStringField(vosk_recognizer_final_result(recognizer),
                                       "text");
    if (text.empty()) {
      text = JsonStringField(vosk_recognizer_result(recognizer), "text");
    }
    vosk_recognizer_free(recognizer);

    text = NormalizeRecognizedText(std::move(text),
                                   concatenate_without_space_);
    if (!text.empty()) {
      events_.push_back({RecognitionEventKind::FinalText, std::move(text), {}});
    }
    events_.push_back({RecognitionEventKind::Completed, {}, {}});
    if (error) {
      error->clear();
    }
    return true;
  }

  void Cancel() override {
    finished_ = true;
    pcm_.clear();
    events_.clear();
    events_.push_back({RecognitionEventKind::Completed, {}, {}});
  }

  std::vector<RecognitionEvent> PollEvents() override {
    auto events = std::move(events_);
    events_.clear();
    return events;
  }

private:
  VoskModel *model_ = nullptr;
  int sample_rate_ = 16000;
  float input_gain_ = 1.0f;
  bool concatenate_without_space_ = false;
  bool finished_ = false;
  std::vector<int16_t> pcm_;
  std::vector<RecognitionEvent> events_;
};

class VoskOfflineBackend : public AsrBackend {
public:
  VoskOfflineBackend(ModelInfo model_info, AsrConfig asr_config,
                     std::string provider_id)
      : model_info_(std::move(model_info)),
        asr_config_(std::move(asr_config)),
        provider_id_(std::move(provider_id)),
        sample_rate_(ResolveSampleRate(model_info_)) {}

  ~VoskOfflineBackend() override {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (model_) {
      vosk_model_free(model_);
      model_ = nullptr;
    }
  }

  BackendDescriptor Describe() const override {
    BackendDescriptor descriptor;
    descriptor.provider_id = provider_id_;
    descriptor.provider_type = vinput::asr::kLocalProviderType;
    descriptor.backend_id = "vosk-offline";
    descriptor.capabilities.audio_delivery_mode = AudioDeliveryMode::Buffered;
    descriptor.capabilities.supports_streaming = false;
    descriptor.capabilities.supports_partial_results = false;
    descriptor.capabilities.supports_hotwords = false;
    return descriptor;
  }

  std::unique_ptr<RecognitionSession>
  CreateSession(std::string *error) override {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (!InitializeModelLocked(error)) {
      return nullptr;
    }

    const std::string language = model_info_.RuntimeLanguageHint();
    if (error) {
      error->clear();
    }
    return std::make_unique<VoskOfflineSession>(
        model_, sample_rate_, asr_config_.input_gain,
        ShouldConcatenateWithoutSpace(language));
  }

private:
  bool InitializeModelLocked(std::string *error) {
    if (model_) {
      if (error) {
        error->clear();
      }
      return true;
    }

    const std::string model_path = model_info_.File("model");
    if (model_path.empty()) {
      if (error) {
        *error = "vosk model path is missing";
      }
      return false;
    }

    vosk_set_log_level(-1);
    model_ = vosk_model_new(model_path.c_str());
    if (!model_) {
      if (error) {
        *error = "failed to load vosk model from '" + model_path + "'";
      }
      return false;
    }

    fprintf(stderr,
            "vinput: vosk offline ASR initialized successfully "
            "(lang: %s, sample_rate: %d)\n",
            asr_config_.language.c_str(), sample_rate_);

    if (error) {
      error->clear();
    }
    return true;
  }

  ModelInfo model_info_;
  AsrConfig asr_config_;
  std::string provider_id_;
  int sample_rate_ = 16000;
  std::mutex model_mutex_;
  VoskModel *model_ = nullptr;
};

}  // namespace

std::unique_ptr<AsrBackend>
CreateVoskOfflineBackend(const CoreConfig &config,
                         const LocalAsrProvider &provider,
                         std::string *error) {
  if (provider.model.empty()) {
    if (error) {
      *error = vinput::str::FmtStr(
          _("Local ASR model configuration is missing for provider '%s'."),
          provider.id);
    }
    return nullptr;
  }

  ModelManager model_mgr(ResolveModelBaseDir(config).string(), provider.model);
  std::string model_error;
  if (!model_mgr.EnsureModels(&model_error)) {
    if (error) {
      *error = "Local ASR model check failed for provider '" + provider.id + "'";
      if (!model_error.empty()) {
        *error += ": " + model_error;
      } else {
        *error += ".";
      }
    }
    return nullptr;
  }

  AsrConfig asr_config;
  const ModelInfo model_info = model_mgr.GetModelInfo();
  asr_config.language = model_info.RuntimeLanguageHint();
  asr_config.hotwords_file = provider.hotwordsFile;
  asr_config.normalize_audio = config.asr.normalizeAudio;
  asr_config.input_gain = static_cast<float>(config.asr.inputGain);
  asr_config.vad_enabled = false;
  asr_config.vad_model_path.clear();

  if (error) {
    error->clear();
  }
  return std::make_unique<VoskOfflineBackend>(model_info,
                                              std::move(asr_config),
                                              provider.id);
}

}  // namespace vinput::daemon::asr
