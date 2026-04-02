#include "daemon/asr/backends/vosk_streaming_backend.h"

#include "common/asr/model_manager.h"
#include "common/i18n.h"
#include "common/utils/string_utils.h"
#include "daemon/asr/asr_config.h"

#include <nlohmann/json.hpp>
#include <vosk_api.h>

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

#ifdef VINPUT_VOSK_HAS_ENDPOINTER
float SafeStof(const std::string &value, float fallback) {
  try {
    return std::stof(value);
  } catch (...) {
    return fallback;
  }
}
#endif

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

#ifdef VINPUT_VOSK_HAS_ENDPOINTER
VoskEndpointerMode ResolveEndpointerMode(const ModelInfo &model_info) {
  const std::string mode =
      model_info.Param("endpointer_mode", model_info.Param("endpoint_mode"));
  if (mode == "short") {
    return VOSK_EP_ANSWER_SHORT;
  }
  if (mode == "long") {
    return VOSK_EP_ANSWER_LONG;
  }
  if (mode == "very_long") {
    return VOSK_EP_ANSWER_VERY_LONG;
  }
  return VOSK_EP_ANSWER_DEFAULT;
}
#endif

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

class VoskStreamingSession : public RecognitionSession {
public:
  VoskStreamingSession(VoskRecognizer *recognizer,
                       bool concatenate_without_space)
      : recognizer_(recognizer),
        concatenate_without_space_(concatenate_without_space) {}

  ~VoskStreamingSession() override { Reset(); }

  bool PushAudio(std::span<const int16_t> pcm, std::string *error) override {
    if (finished_) {
      if (error) {
        *error = "Recognition session already finished.";
      }
      return false;
    }

    if (!recognizer_) {
      if (error) {
        *error = "Vosk recognizer is not initialized.";
      }
      return false;
    }

    if (pcm.empty()) {
      if (error) {
        error->clear();
      }
      return true;
    }

    std::vector<short> samples(pcm.begin(), pcm.end());

    const int accepted = vosk_recognizer_accept_waveform_s(
        recognizer_, samples.data(), static_cast<int>(samples.size()));
    if (accepted < 0) {
      if (error) {
        *error = "vosk failed to accept audio chunk";
      }
      return false;
    }

    if (accepted > 0) {
      QueueIntermediateFinal();
    } else {
      QueuePartial(vosk_recognizer_partial_result(recognizer_));
    }

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

    if (!recognizer_) {
      if (error) {
        *error = "Vosk recognizer is not initialized.";
      }
      return false;
    }

    finished_ = true;
    QueueFinal(vosk_recognizer_final_result(recognizer_));
    events_.push_back({RecognitionEventKind::Completed, {}, {}});

    if (error) {
      error->clear();
    }
    return true;
  }

  void Cancel() override {
    finished_ = true;
    events_.clear();
    events_.push_back({RecognitionEventKind::Completed, {}, {}});
    Reset();
  }

  std::vector<RecognitionEvent> PollEvents() override {
    auto events = std::move(events_);
    events_.clear();
    return events;
  }

private:
  std::string JoinCommittedAndCurrent(std::string_view current) const {
    if (committed_text_.empty()) {
      return std::string(current);
    }
    if (current.empty()) {
      return committed_text_;
    }
    if (concatenate_without_space_) {
      return committed_text_ + std::string(current);
    }
    return committed_text_ + " " + std::string(current);
  }

  void CommitRecognizedText(std::string text) {
    text = NormalizeRecognizedText(std::move(text), concatenate_without_space_);
    if (text.empty()) {
      return;
    }
    if (committed_text_.empty()) {
      committed_text_ = std::move(text);
    } else {
      if (concatenate_without_space_) {
        committed_text_ += text;
      } else {
        committed_text_ += " " + text;
      }
    }
    last_final_candidate_text_ = committed_text_;
  }

  void QueueTextEvent(RecognitionEventKind kind, std::string text) {
    if (text.empty()) {
      return;
    }
    if (kind == RecognitionEventKind::PartialText) {
      if (text == last_partial_text_) {
        return;
      }
      last_partial_text_ = text;
    } else if (kind == RecognitionEventKind::FinalText) {
      last_partial_text_ = text;
      last_final_candidate_text_ = text;
    }
    events_.push_back({kind, std::move(text), {}});
  }

  void QueuePartial(const char *raw_json) {
    QueueTextEvent(RecognitionEventKind::PartialText,
                   JoinCommittedAndCurrent(NormalizeRecognizedText(
                       JsonStringField(raw_json, "partial"),
                       concatenate_without_space_)));
  }

  void QueueIntermediateFinal() {
    const std::string text = NormalizeRecognizedText(
        JsonStringField(vosk_recognizer_result(recognizer_), "text"),
        concatenate_without_space_);
    if (text.empty()) {
      return;
    }

    CommitRecognizedText(text);
    QueueTextEvent(RecognitionEventKind::PartialText, committed_text_);
  }

  void QueueFinal(const char *raw_json) {
    std::string text = JsonStringField(raw_json, "text");
    if (text.empty()) {
      text = JsonStringField(vosk_recognizer_result(recognizer_), "text");
    }
    text = NormalizeRecognizedText(std::move(text),
                                   concatenate_without_space_);

    if (!text.empty()) {
      const std::string full_text = JoinCommittedAndCurrent(text);
      if (full_text != committed_text_) {
        last_final_candidate_text_ = full_text;
      }
      text = std::move(full_text);
    } else if (!last_final_candidate_text_.empty()) {
      text = last_final_candidate_text_;
    } else if (!last_partial_text_.empty()) {
      text = last_partial_text_;
    } else {
      text = committed_text_;
    }
    if (!text.empty() && text != last_partial_text_) {
      QueueTextEvent(RecognitionEventKind::PartialText, text);
    }
    QueueTextEvent(RecognitionEventKind::FinalText, text);
  }

  void Reset() {
    if (recognizer_) {
      vosk_recognizer_free(recognizer_);
      recognizer_ = nullptr;
    }
  }

  VoskRecognizer *recognizer_ = nullptr;
  bool finished_ = false;
  bool concatenate_without_space_ = false;
  std::string committed_text_;
  std::string last_partial_text_;
  std::string last_final_candidate_text_;
  std::vector<RecognitionEvent> events_;
};

class VoskStreamingBackend : public AsrBackend {
public:
  VoskStreamingBackend(ModelInfo model_info, AsrConfig asr_config,
                       std::string provider_id)
      : model_info_(std::move(model_info)),
        asr_config_(std::move(asr_config)),
        provider_id_(std::move(provider_id)),
        sample_rate_(ResolveSampleRate(model_info_)) {}

  ~VoskStreamingBackend() override {
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
    descriptor.backend_id = "vosk-streaming";
    descriptor.capabilities.audio_delivery_mode = AudioDeliveryMode::Chunked;
    descriptor.capabilities.supports_streaming = true;
    descriptor.capabilities.supports_partial_results = true;
    descriptor.capabilities.supports_hotwords = false;
    return descriptor;
  }

  std::unique_ptr<RecognitionSession>
  CreateSession(std::string *error) override {
    std::lock_guard<std::mutex> lock(model_mutex_);
    if (!InitializeModelLocked(error)) {
      return nullptr;
    }

    VoskRecognizer *recognizer =
        vosk_recognizer_new(model_, static_cast<float>(sample_rate_));
    if (!recognizer) {
      if (error) {
        *error = "failed to create vosk recognizer";
      }
      return nullptr;
    }

    vosk_recognizer_set_words(recognizer, 0);
    vosk_recognizer_set_partial_words(recognizer, 0);
    vosk_recognizer_set_max_alternatives(recognizer, 0);

#ifdef VINPUT_VOSK_HAS_ENDPOINTER
    vosk_recognizer_set_endpointer_mode(recognizer,
                                        ResolveEndpointerMode(model_info_));

    const float t_start_max = SafeStof(model_info_.Param("t_start_max", "8.0"), 8.0f);
    const float t_end = SafeStof(model_info_.Param("t_end", "0.6"), 0.6f);
    const float t_max = SafeStof(model_info_.Param("t_max", "30.0"), 30.0f);
    vosk_recognizer_set_endpointer_delays(recognizer, t_start_max, t_end, t_max);
#endif

    if (error) {
      error->clear();
    }
    const std::string language = model_info_.RuntimeLanguageHint();
    return std::make_unique<VoskStreamingSession>(recognizer,
                                                  ShouldConcatenateWithoutSpace(
                                                      language));
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
            "vinput: vosk streaming ASR initialized successfully "
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
CreateVoskStreamingBackend(const CoreConfig &config,
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
  asr_config.vad_enabled = false;
  asr_config.vad_model_path.clear();

  if (error) {
    error->clear();
  }
  return std::make_unique<VoskStreamingBackend>(model_info,
                                                std::move(asr_config),
                                                provider.id);
}

}  // namespace vinput::daemon::asr
