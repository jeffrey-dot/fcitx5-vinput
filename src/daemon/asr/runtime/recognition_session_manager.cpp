#include "daemon/asr/runtime/recognition_session_manager.h"

#include "common/i18n.h"
#include "common/utils/debug_log.h"
#include "daemon/asr/runtime/backend_factory.h"

#include <utility>

namespace vinput::daemon::asr {

namespace {

bool ShouldDisableAsr(const CoreConfig &config, bool disable_asr_by_flag,
                      std::string *reason) {
  if (disable_asr_by_flag) {
    if (reason) {
      *reason = _("ASR disabled by command line.");
    }
    return true;
  }

  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    if (reason) {
      *reason = _("No active ASR provider configured.");
    }
    return true;
  }

  return false;
}

std::string BuildRuntimeSignature(const CoreConfig &config) {
  return nlohmann::ordered_json(config).dump();
}

void LogActiveBackend(const CoreConfig &config) {
  BackendDescriptor descriptor;
  std::string error;
  if (!DescribeActiveBackend(config, &descriptor, &error)) {
    vinput::debug::Log("ASR provider=(missing)\n");
    return;
  }

  vinput::debug::Log("ASR provider=%s type=%s backend=%s lang=%s\n",
                     descriptor.provider_id.c_str(),
                     descriptor.provider_type.c_str(),
                     descriptor.backend_id.c_str(),
                     config.global.defaultLanguage.c_str());
}

void LogRecognitionRequest(const CoreConfig &config, std::size_t sample_count) {
  BackendDescriptor descriptor;
  std::string error;
  if (!DescribeActiveBackend(config, &descriptor, &error)) {
    vinput::debug::Log("ASR request provider=(missing) samples=%zu\n",
                       sample_count);
    return;
  }

  vinput::debug::Log("ASR request provider=%s type=%s backend=%s samples=%zu\n",
                     descriptor.provider_id.c_str(),
                     descriptor.provider_type.c_str(),
                     descriptor.backend_id.c_str(), sample_count);
}

}  // namespace

RecognitionSessionManager::RecognitionSessionManager(bool disable_asr_by_flag)
    : disable_asr_by_flag_(disable_asr_by_flag) {}

RecognitionSessionManager::~RecognitionSessionManager() { Shutdown(); }

bool RecognitionSessionManager::Initialize(const CoreConfig &settings,
                                           std::string *disabled_reason) {
  if (!EnsureBackendReady(settings, disabled_reason)) {
    return false;
  }

  if (!backend_) {
    return true;
  }

  const auto descriptor = backend_->Describe();
  if (!descriptor.capabilities.supports_streaming) {
    return true;
  }

  std::string warmup_error;
  auto warmup_session = backend_->CreateSession(&warmup_error);
  if (!warmup_session) {
    if (disabled_reason) {
      *disabled_reason = std::move(warmup_error);
    }
    return false;
  }

  warmup_session->Cancel();
  return true;
}

std::unique_ptr<RecognitionSession> RecognitionSessionManager::CreateSession(
    const CoreConfig &settings, BackendDescriptor *descriptor,
    std::string *error) {
  if (!EnsureBackendReady(settings, error)) {
    return nullptr;
  }

  if (descriptor) {
    *descriptor = backend_->Describe();
  }

  auto session = backend_->CreateSession(error);
  if (!session) {
    return nullptr;
  }

  if (error) {
    error->clear();
  }
  return session;
}

RecognitionRunResult RecognitionSessionManager::ConsumeEvents(
    std::unique_ptr<RecognitionSession> *session, bool cancel,
    std::string *error) {
  RecognitionRunResult result;
  if (!session || !*session) {
    if (error) {
      *error = "Recognition session is not initialized.";
    }
    result.ok = false;
    result.error = error ? *error : "Recognition session is not initialized.";
    return result;
  }

  result.available = true;
  std::string session_error;
  if (cancel) {
    (*session)->Cancel();
  } else if (!(*session)->Finish(&session_error) && !session_error.empty()) {
    result.ok = false;
    result.error = session_error;
  }

  for (auto &event : (*session)->PollEvents()) {
    switch (event.kind) {
    case RecognitionEventKind::PartialText:
      break;
    case RecognitionEventKind::FinalText:
      result.text = std::move(event.text);
      break;
    case RecognitionEventKind::Error:
      result.ok = false;
      result.error = std::move(event.error);
      break;
    case RecognitionEventKind::Completed:
      break;
    }
  }

  session->reset();
  if (error) {
    *error = result.error;
  }
  return result;
}

RecognitionRunResult RecognitionSessionManager::Recognize(
    const CoreConfig &settings, const std::vector<int16_t> &pcm_data) {
  RecognitionRunResult result;

  BackendDescriptor descriptor;
  std::string error;
  auto session = CreateSession(settings, &descriptor, &error);
  if (!session) {
    result.available = false;
    result.ok = false;
    result.error = std::move(error);
    return result;
  }

  result.available = true;
  LogRecognitionRequest(settings, pcm_data.size());

  if (!session->PushAudio(pcm_data, &error)) {
    result.ok = false;
    result.error = std::move(error);
    return result;
  }

  return ConsumeEvents(&session, false, &error);
}

void RecognitionSessionManager::Shutdown() { ResetBackend(); }

bool RecognitionSessionManager::EnsureBackendReady(const CoreConfig &settings,
                                                   std::string *error) {
  std::string disabled_reason;
  if (ShouldDisableAsr(settings, disable_asr_by_flag_, &disabled_reason)) {
    ResetBackend();
    if (error) {
      *error = std::move(disabled_reason);
    }
    return false;
  }

  const std::string signature = BuildRuntimeSignature(settings);
  if (backend_ && backend_signature_ == signature) {
    if (error) {
      error->clear();
    }
    return true;
  }

  ResetBackend();
  LogActiveBackend(settings);

  std::string init_error;
  backend_ = CreateBackend(settings, &init_error);
  if (!backend_) {
    if (error) {
      *error = std::move(init_error);
    }
    return false;
  }

  backend_signature_ = signature;
  if (error) {
    error->clear();
  }
  return true;
}

void RecognitionSessionManager::ResetBackend() {
  backend_.reset();
  backend_signature_.clear();
}

}  // namespace vinput::daemon::asr
