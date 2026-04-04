#include "daemon/asr/runtime/recognition_session_manager.h"

#include "common/i18n.h"
#include "common/utils/debug_log.h"
#include "daemon/asr/runtime/backend_factory.h"

#include <cstdio>
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
  nlohmann::ordered_json j;
  j["default_language"] = config.global.defaultLanguage;
  j["active_provider"] = config.asr.activeProvider;

  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    return j.dump();
  }

  nlohmann::ordered_json provider_json;
  provider_json["id"] = AsrProviderId(*provider);
  provider_json["type"] = std::string(AsrProviderType(*provider));
  provider_json["timeout_ms"] = AsrProviderTimeoutMs(*provider);

  if (const auto *local = std::get_if<LocalAsrProvider>(provider)) {
    provider_json["model"] = local->model;
    provider_json["hotwords_file"] = local->hotwordsFile;
    provider_json["vad_enabled"] = config.asr.vad.enabled;
  } else if (const auto *command = std::get_if<CommandAsrProvider>(provider)) {
    provider_json["command"] = command->command;
    provider_json["args"] = command->args;
    provider_json["env"] = command->env;
  }

  j["provider"] = std::move(provider_json);
  return j.dump();
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

void LogRecognitionRequest(const BackendDescriptor &descriptor,
                           std::size_t sample_count) {
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
  std::string disabled_state_reason;
  if (ShouldDisableAsr(settings, disable_asr_by_flag_,
                       &disabled_state_reason)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ApplyDisabledStateLocked();
    if (disabled_reason) {
      *disabled_reason = std::move(disabled_state_reason);
    }
    return false;
  }

  PreparedBackend prepared;
  std::string error;
  if (!CreatePreparedBackend(settings, &prepared, &error)) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      ResetEffectiveBackendLocked();
      target_backend_signature_.clear();
      last_reload_error_ = error;
    }
    if (disabled_reason) {
      *disabled_reason = error;
    }
    return false;
  }

  if (!ActivatePreparedBackend(std::move(prepared), &error)) {
    if (disabled_reason) {
      *disabled_reason = error;
    }
    return false;
  }

  EnsureReloadWorkerStarted();
  if (disabled_reason) {
    disabled_reason->clear();
  }
  return true;
}

bool RecognitionSessionManager::SynchronizeBackend(const CoreConfig &settings,
                                                   std::string *error) {
  std::string disabled_reason;
  if (ShouldDisableAsr(settings, disable_asr_by_flag_, &disabled_reason)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ApplyDisabledStateLocked();
    if (error) {
      error->clear();
    }
    return true;
  }

  EnsureReloadWorkerStarted();

  const std::string signature = BuildRuntimeSignature(settings);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (effective_backend_ && effective_backend_signature_ == signature &&
        !reload_in_progress_) {
      const AsrProvider *provider = ResolveActiveAsrProvider(settings);
      target_provider_id_ = provider ? AsrProviderId(*provider) : std::string{};
      target_model_id_ = ResolvePreferredLocalModel(settings);
      target_backend_signature_ = signature;
      last_reload_error_.clear();
      if (error) {
        error->clear();
      }
      return true;
    }

    pending_settings_ = settings;
    const AsrProvider *provider = ResolveActiveAsrProvider(settings);
    target_provider_id_ = provider ? AsrProviderId(*provider) : std::string{};
    target_model_id_ = ResolvePreferredLocalModel(settings);
    target_backend_signature_ = signature;
    reload_requested_ = true;
    last_reload_error_.clear();
  }

  reload_cv_.notify_one();
  if (error) {
    error->clear();
  }
  return true;
}

std::unique_ptr<RecognitionSession> RecognitionSessionManager::CreateSession(
    const CoreConfig &settings, BackendDescriptor *descriptor,
    std::string *error) {
  (void)settings;

  std::unique_ptr<RecognitionSession> session;
  if (!CreateSessionFromEffectiveBackend(descriptor, &session, error)) {
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
  LogRecognitionRequest(descriptor, pcm_data.size());

  if (!session->PushAudio(pcm_data, &error)) {
    result.ok = false;
    result.error = std::move(error);
    return result;
  }

  return ConsumeEvents(&session, false, &error);
}

RecognitionSessionManager::ReloadSnapshot
RecognitionSessionManager::GetReloadSnapshot() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return ReloadSnapshot{
      .target_provider_id = target_provider_id_,
      .target_model_id = target_model_id_,
      .effective_provider_id = effective_provider_id_,
      .effective_model_id = effective_model_id_,
      .target_signature = target_backend_signature_,
      .effective_signature = effective_backend_signature_,
      .last_error = last_reload_error_,
      .reload_in_progress = reload_in_progress_,
      .has_effective_backend = effective_backend_ != nullptr,
  };
}

void RecognitionSessionManager::SetReloadResultCallback(
    std::function<void(bool success, const std::string &message)> callback) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  reload_result_callback_ = std::move(callback);
}

void RecognitionSessionManager::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    reload_worker_running_ = false;
    reload_requested_ = false;
  }
  reload_cv_.notify_all();
  if (reload_worker_.joinable()) {
    reload_worker_.join();
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  ResetEffectiveBackendLocked();
  target_provider_id_.clear();
  target_model_id_.clear();
  target_backend_signature_.clear();
  last_reload_error_.clear();
}

bool RecognitionSessionManager::CreatePreparedBackend(
    const CoreConfig &settings, PreparedBackend *prepared, std::string *error) {
  if (!prepared) {
    if (error) {
      *error = "Prepared backend output is not available.";
    }
    return false;
  }

  LogActiveBackend(settings);
  std::string create_error;
  auto backend = CreateBackend(settings, &create_error);
  if (!backend) {
    if (error) {
      *error = std::move(create_error);
    }
    return false;
  }

  const BackendDescriptor descriptor = backend->Describe();

  // Force backend initialization during preparation so recording does not
  // become the hidden fallback path for expensive model load.
  std::string warmup_error;
  auto warmup_session = backend->CreateSession(&warmup_error);
  if (!warmup_session) {
    if (error) {
      *error = std::move(warmup_error);
    }
    return false;
  }
  warmup_session->Cancel();

  prepared->backend = std::move(backend);
  prepared->descriptor = descriptor;
  prepared->provider_id = descriptor.provider_id;
  prepared->model_id = ResolvePreferredLocalModel(settings);
  prepared->signature = BuildRuntimeSignature(settings);
  if (error) {
    error->clear();
  }
  return true;
}

bool RecognitionSessionManager::ActivatePreparedBackend(
    PreparedBackend prepared, std::string *error) {
  if (!prepared.backend) {
    if (error) {
      *error = "Prepared backend is empty.";
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    effective_backend_ = std::move(prepared.backend);
    effective_descriptor_ = std::move(prepared.descriptor);
    has_effective_descriptor_ = true;
    effective_provider_id_ = std::move(prepared.provider_id);
    effective_model_id_ = std::move(prepared.model_id);
    effective_backend_signature_ = std::move(prepared.signature);
    target_provider_id_ = effective_provider_id_;
    target_model_id_ = effective_model_id_;
    target_backend_signature_ = effective_backend_signature_;
    last_reload_error_.clear();
  }

  if (error) {
    error->clear();
  }
  return true;
}

bool RecognitionSessionManager::CreatePreparedBackendForReload(
    const CoreConfig &settings, const std::string &signature,
    PreparedBackend *prepared, std::string *error) {
  if (!CreatePreparedBackend(settings, prepared, error)) {
    return false;
  }
  prepared->signature = signature;
  return true;
}

bool RecognitionSessionManager::CreateSessionFromEffectiveBackend(
    BackendDescriptor *descriptor, std::unique_ptr<RecognitionSession> *session,
    std::string *error) {
  if (!session) {
    if (error) {
      *error = "Recognition session output is not available.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!effective_backend_) {
    if (error) {
      if (!last_reload_error_.empty()) {
        *error = last_reload_error_;
      } else if (reload_in_progress_) {
        *error = "ASR backend is still loading.";
      } else {
        *error = "ASR backend is not ready.";
      }
    }
    return false;
  }

  if (descriptor && has_effective_descriptor_) {
    *descriptor = effective_descriptor_;
  }

  *session = effective_backend_->CreateSession(error);
  return static_cast<bool>(*session);
}

void RecognitionSessionManager::EnsureReloadWorkerStarted() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (reload_worker_running_) {
    return;
  }
  reload_worker_running_ = true;
  reload_worker_ = std::thread([this]() { ReloadWorkerMain(); });
}

void RecognitionSessionManager::ReloadWorkerMain() {
  while (true) {
    CoreConfig settings;
    std::string signature;
    {
      std::unique_lock<std::mutex> lock(state_mutex_);
      reload_cv_.wait(lock, [&]() {
        return reload_requested_ || !reload_worker_running_;
      });
      if (!reload_worker_running_) {
        break;
      }

      settings = pending_settings_;
      signature = target_backend_signature_;
      reload_requested_ = false;
      reload_in_progress_ = true;
    }

    PreparedBackend prepared;
    std::string error;
    const bool ok =
        CreatePreparedBackendForReload(settings, signature, &prepared, &error);

    if (ok) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        effective_backend_ = std::move(prepared.backend);
        effective_descriptor_ = std::move(prepared.descriptor);
        has_effective_descriptor_ = true;
        effective_provider_id_ = std::move(prepared.provider_id);
        effective_model_id_ = std::move(prepared.model_id);
        effective_backend_signature_ = std::move(prepared.signature);
        last_reload_error_.clear();
        reload_in_progress_ = false;
      }
      vinput::debug::Log("ASR backend reload applied asynchronously\n");
      NotifyReloadResult(true, {});
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      last_reload_error_ = error;
      reload_in_progress_ = false;
    }
    fprintf(stderr, "vinput-daemon: async ASR backend reload failed: %s\n",
            error.c_str());
    NotifyReloadResult(false, error);
  }
}

void RecognitionSessionManager::ResetEffectiveBackendLocked() {
  effective_backend_.reset();
  has_effective_descriptor_ = false;
  effective_descriptor_ = {};
  effective_provider_id_.clear();
  effective_model_id_.clear();
  effective_backend_signature_.clear();
}

void RecognitionSessionManager::ApplyDisabledStateLocked() {
  pending_settings_ = {};
  target_provider_id_.clear();
  target_model_id_.clear();
  target_backend_signature_.clear();
  last_reload_error_.clear();
  reload_requested_ = false;
  reload_in_progress_ = false;
  ResetEffectiveBackendLocked();
}

void RecognitionSessionManager::NotifyReloadResult(
    bool success, const std::string &message) {
  std::function<void(bool, const std::string &)> callback;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    callback = reload_result_callback_;
  }
  if (callback) {
    callback(success, message);
  }
}

}  // namespace vinput::daemon::asr
