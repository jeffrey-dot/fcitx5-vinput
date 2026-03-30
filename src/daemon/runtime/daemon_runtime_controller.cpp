#include "daemon/runtime/daemon_runtime_controller.h"

#include "common/config/core_config.h"
#include "common/dbus/dbus_interface.h"
#include "common/utils/debug_log.h"
#include "daemon/audio/audio_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <utility>

namespace vinput::daemon::runtime {

namespace {

constexpr std::size_t kStreamingChunkSamples = 800;
constexpr float kNonSilentPeakThreshold = 0.02f;
constexpr float kNonSilentRmsThreshold = 0.005f;

void LogRecognitionRequest(
    const vinput::daemon::asr::BackendDescriptor &descriptor,
    std::size_t sample_count) {
  vinput::debug::Log("ASR request provider=%s type=%s backend=%s samples=%zu\n",
                     descriptor.provider_id.c_str(),
                     descriptor.provider_type.c_str(),
                     descriptor.backend_id.c_str(), sample_count);
}

bool UsesBufferedDelivery(
    const vinput::daemon::asr::BackendDescriptor &descriptor) {
  return descriptor.capabilities.audio_delivery_mode ==
         vinput::daemon::asr::AudioDeliveryMode::Buffered;
}

bool HasNonSilentAudio(std::span<const int16_t> pcm) {
  if (pcm.empty()) {
    return false;
  }

  double sum_squares = 0.0;
  float peak = 0.0f;
  for (int16_t sample : pcm) {
    const float value = static_cast<float>(sample) / 32768.0f;
    const float abs_value = std::fabs(value);
    peak = std::max(peak, abs_value);
    sum_squares += static_cast<double>(value) * static_cast<double>(value);
  }

  const float rms = static_cast<float>(std::sqrt(sum_squares / pcm.size()));
  return peak >= kNonSilentPeakThreshold || rms >= kNonSilentRmsThreshold;
}

long MillisecondsSince(
    const std::optional<std::chrono::steady_clock::time_point> &start,
    std::chrono::steady_clock::time_point end) {
  if (!start.has_value()) {
    return -1;
  }
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                               end - *start)
                               .count());
}

}  // namespace

DaemonRuntimeController::DaemonRuntimeController(
    AudioCapture *capture, DbusService *dbus,
    vinput::daemon::asr::RecognitionSessionManager *recognition_manager,
    RecognitionPipeline *pipeline)
    : capture_(capture),
      dbus_(dbus),
      recognition_manager_(recognition_manager),
      pipeline_(pipeline) {}

DaemonRuntimeController::~DaemonRuntimeController() { Shutdown(); }

DbusService::MethodResult DaemonRuntimeController::StartRecording() {
  return StartRecordingInternal(false, {});
}

DbusService::MethodResult DaemonRuntimeController::StartCommandRecording(
    const std::string &selected_text) {
  return StartRecordingInternal(true, selected_text);
}

DbusService::MethodResult DaemonRuntimeController::StartRecordingInternal(
    bool is_command, const std::string &selected_text) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (phase_ != vinput::dbus::Status::Idle) {
    vinput::debug::Log("start rejected (phase: %s)\n",
                       vinput::dbus::StatusToString(phase_));
    return DbusService::MethodResult::Failure("Daemon is busy.");
  }

  auto runtime_settings = LoadCoreConfig();
  NormalizeCoreConfig(&runtime_settings);

  std::string error;
  auto session = recognition_manager_->CreateSession(runtime_settings,
                                                     &active_backend_, &error);
  if (!session) {
    std::string message = "Failed to start recognition session.";
    if (!error.empty()) {
      message += " " + error;
    }
    fprintf(stderr, "vinput-daemon: %s\n", message.c_str());
    return DbusService::MethodResult::Failure(message);
  }

  current_order_.reset();
  active_session_ = std::move(session);
  current_is_command_ = is_command;
  current_selected_text_ = selected_text;
  current_recording_pcm_.clear();
  pending_chunk_pcm_.clear();
  current_sample_count_ = 0;
  current_input_gain_ = static_cast<float>(runtime_settings.asr.inputGain);
  latest_final_text_.clear();
  recording_started_at_ = std::chrono::steady_clock::now();
  first_non_silent_at_.reset();
  first_partial_logged_ = false;
  accepting_chunks_.store(true, std::memory_order_relaxed);

  capture_->SetTargetObject(runtime_settings.global.captureDevice);
  capture_->SetChunkCallback(
      [this](std::span<const int16_t> pcm) { HandleIncomingAudio(pcm); });
  if (!capture_->BeginRecording(&error)) {
    std::string message =
        is_command ? "Failed to start command recording."
                   : "Failed to start recording.";
    if (!error.empty()) {
      message = message.substr(0, message.size() - 1) + ": " + error;
    }
    accepting_chunks_.store(false, std::memory_order_relaxed);
    CancelActiveSession();
    fprintf(stderr, "vinput-daemon: %s\n", message.c_str());
    return DbusService::MethodResult::Failure(message);
  }

  phase_ = vinput::dbus::Status::Recording;
  dbus_->EmitStatusChanged(
      vinput::dbus::StatusToString(vinput::dbus::Status::Recording));
  if (is_command) {
    vinput::debug::Log(
        "command recording started (selected_text length: %zu chars)\n",
        selected_text.size());
  } else {
    vinput::debug::Log("recording started\n");
  }
  return DbusService::MethodResult::Success();
}

void DaemonRuntimeController::HandleIncomingAudio(std::span<const int16_t> pcm) {
  if (!accepting_chunks_.load(std::memory_order_relaxed) || pcm.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!active_session_) {
    return;
  }

  // Apply gain at the device boundary so all backends see processed audio.
  std::vector<int16_t> gained_pcm(pcm.begin(), pcm.end());
  if (current_input_gain_ != 1.0f) {
    vinput::audio::ApplyGainI16(gained_pcm, current_input_gain_);
  }
  const std::span<const int16_t> pcm_view(gained_pcm);

  if (!first_non_silent_at_.has_value() &&
      HasNonSilentAudio(pcm_view)) {
    first_non_silent_at_ = std::chrono::steady_clock::now();
    vinput::debug::Log("first non-silent audio after %ld ms\n",
                       MillisecondsSince(recording_started_at_,
                                         *first_non_silent_at_));
  }

  if (UsesBufferedDelivery(active_backend_)) {
    current_recording_pcm_.insert(current_recording_pcm_.end(), pcm_view.begin(),
                                  pcm_view.end());
    current_sample_count_ += pcm_view.size();
  } else {
    pending_chunk_pcm_.insert(pending_chunk_pcm_.end(), pcm_view.begin(), pcm_view.end());
    std::string error;
    while (pending_chunk_pcm_.size() >= kStreamingChunkSamples) {
      if (!active_session_->PushAudio(
              std::span<const int16_t>(pending_chunk_pcm_.data(),
                                       kStreamingChunkSamples),
              &error)) {
        fprintf(stderr, "vinput-daemon: failed to push audio chunk: %s\n",
                error.c_str());
        accepting_chunks_.store(false, std::memory_order_relaxed);
        active_session_->Cancel();
        active_session_.reset();
        current_recording_pcm_.clear();
        pending_chunk_pcm_.clear();
        phase_ = vinput::dbus::Status::Error;
        dbus_->EmitStatusChanged(
            vinput::dbus::StatusToString(vinput::dbus::Status::Error));
        if (!error.empty()) {
          dbus_->EmitError(vinput::dbus::MakeRawError(error));
        }
        capture_->EndRecording();
        capture_->Stop();
        phase_ = vinput::dbus::Status::Idle;
        dbus_->EmitStatusChanged(
            vinput::dbus::StatusToString(vinput::dbus::Status::Idle));
        return;
      }
      current_sample_count_ += kStreamingChunkSamples;
      EmitStreamingEvents(active_session_.get());
      pending_chunk_pcm_.erase(
          pending_chunk_pcm_.begin(),
          pending_chunk_pcm_.begin() +
              static_cast<std::ptrdiff_t>(kStreamingChunkSamples));
    }
  }
}

void DaemonRuntimeController::EmitStreamingEvents(
    vinput::daemon::asr::RecognitionSession *session,
    std::string *latest_partial_text) {
  if (!session) {
    return;
  }

  for (auto &event : session->PollEvents()) {
    switch (event.kind) {
    case vinput::daemon::asr::RecognitionEventKind::PartialText:
      if (!event.text.empty()) {
        if (!first_partial_logged_) {
          first_partial_logged_ = true;
          const auto now = std::chrono::steady_clock::now();
          const long first_non_silent_ms =
              first_non_silent_at_.has_value()
                  ? MillisecondsSince(recording_started_at_, *first_non_silent_at_)
                  : -1;
          vinput::debug::Log(
              "first partial after %ld ms (first_non_silent_after=%ld ms)\n",
              MillisecondsSince(recording_started_at_, now),
              first_non_silent_ms);
        }
        if (latest_partial_text) {
          *latest_partial_text = event.text;
        }
        dbus_->EmitRecognitionPartial(event.text);
      }
      break;
    case vinput::daemon::asr::RecognitionEventKind::FinalText:
      if (!event.text.empty()) {
        latest_final_text_ = event.text;
        if (latest_partial_text) {
          *latest_partial_text = event.text;
        }
        dbus_->EmitRecognitionPartial(event.text);
      }
      break;
    case vinput::daemon::asr::RecognitionEventKind::Error:
      if (!event.error.empty()) {
        dbus_->EmitError(vinput::dbus::ClassifyErrorText(event.error));
      }
      break;
    case vinput::daemon::asr::RecognitionEventKind::Completed:
      break;
    }
  }
}

DbusService::MethodResult DaemonRuntimeController::StopRecording(
    const std::string &scene_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (phase_ != vinput::dbus::Status::Recording) {
    vinput::debug::Log("stop rejected (phase: %s)\n",
                       vinput::dbus::StatusToString(phase_));
    return DbusService::MethodResult::Failure("Recording is not active.");
  }

  capture_->EndRecording();
  auto captured_pcm = capture_->StopAndGetBuffer();
  accepting_chunks_.store(false, std::memory_order_relaxed);

  if (!active_session_) {
    vinput::debug::Log("recording stopped without active session\n");
    phase_ = vinput::dbus::Status::Idle;
    current_order_.reset();
    dbus_->EmitStatusChanged(
        vinput::dbus::StatusToString(vinput::dbus::Status::Idle));
    vinput::debug::Log("phase -> idle\n");
    return DbusService::MethodResult::Success();
  }

  if (UsesBufferedDelivery(active_backend_)) {
    current_recording_pcm_ = std::move(captured_pcm);
    current_sample_count_ = current_recording_pcm_.size();
  } else {
    if (captured_pcm.size() > current_sample_count_ + pending_chunk_pcm_.size()) {
      pending_chunk_pcm_.insert(
          pending_chunk_pcm_.end(),
          captured_pcm.begin() +
              static_cast<std::ptrdiff_t>(current_sample_count_ +
                                          pending_chunk_pcm_.size()),
          captured_pcm.end());
    }

    if (!pending_chunk_pcm_.empty()) {
      const std::size_t tail_samples = pending_chunk_pcm_.size();
      std::string error;
      if (!active_session_->PushAudio(
              std::span<const int16_t>(pending_chunk_pcm_.data(), tail_samples),
              &error)) {
        fprintf(stderr,
                "vinput-daemon: failed to push final audio tail chunk: %s\n",
                error.c_str());
        CancelActiveSession();
        phase_ = vinput::dbus::Status::Error;
        dbus_->EmitStatusChanged(
            vinput::dbus::StatusToString(vinput::dbus::Status::Error));
        if (!error.empty()) {
          dbus_->EmitError(vinput::dbus::MakeRawError(error));
        }
        phase_ = vinput::dbus::Status::Idle;
        dbus_->EmitStatusChanged(
            vinput::dbus::StatusToString(vinput::dbus::Status::Idle));
        vinput::debug::Log("phase -> idle\n");
        return DbusService::MethodResult::Success();
      }
      current_sample_count_ += tail_samples;
      EmitStreamingEvents(active_session_.get());
      vinput::debug::Log(
          "flushed final audio tail chunk samples=%zu captured=%zu "
          "chunk_size=%zu\n",
          tail_samples, captured_pcm.size(), kStreamingChunkSamples);
      pending_chunk_pcm_.clear();
    }
  }

  if (current_sample_count_ < vinput::daemon::asr::kMinSamplesForRecognition) {
    vinput::debug::Log(
        "recording too short, skipping inference: %zu samples (%.1f ms)\n",
        current_sample_count_,
        static_cast<double>(current_sample_count_) * 1000.0 / 16000.0);
    CancelActiveSession();
    phase_ = vinput::dbus::Status::Idle;
    current_order_.reset();
    dbus_->EmitStatusChanged(
        vinput::dbus::StatusToString(vinput::dbus::Status::Idle));
    vinput::debug::Log("phase -> idle\n");
    return DbusService::MethodResult::Success();
  }

  current_order_ = RecognitionOrder{};
  current_order_->audio_delivery_mode =
      active_backend_.capabilities.audio_delivery_mode;
  current_order_->session = std::move(active_session_);
  current_order_->backend = active_backend_;
  current_order_->pcm = std::move(current_recording_pcm_);
  current_order_->recognized_text = latest_final_text_;
  current_order_->scene_id = scene_id;
  current_order_->is_command = current_is_command_;
  current_order_->selected_text = current_selected_text_;
  current_is_command_ = false;
  current_selected_text_.clear();
  LogRecognitionRequest(active_backend_, current_sample_count_);
  current_sample_count_ = 0;
  active_backend_ = {};
  phase_ = vinput::dbus::Status::Inferring;
  dbus_->EmitStatusChanged(
      vinput::dbus::StatusToString(vinput::dbus::Status::Inferring));
  worker_cv_.notify_one();
  vinput::debug::Log("recording stopped\n");
  return DbusService::MethodResult::Success();
}

void DaemonRuntimeController::CancelActiveSession() {
  if (!active_session_) {
    return;
  }
  active_session_->Cancel();
  active_session_.reset();
  current_recording_pcm_.clear();
  pending_chunk_pcm_.clear();
  current_sample_count_ = 0;
  current_input_gain_ = 1.0f;
  latest_final_text_.clear();
  recording_started_at_.reset();
  first_non_silent_at_.reset();
  first_partial_logged_ = false;
  active_backend_ = {};
}

std::string DaemonRuntimeController::GetStatus() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return vinput::dbus::StatusToString(phase_);
}

void DaemonRuntimeController::StartWorker() {
  if (worker_running_) {
    return;
  }
  worker_running_ = true;
  worker_ = std::thread([this]() { WorkerMain(); });
}

void DaemonRuntimeController::Shutdown() {
  capture_->SetChunkCallback({});
  accepting_chunks_.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    CancelActiveSession();
    worker_running_ = false;
  }
  worker_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void DaemonRuntimeController::SetPhase(vinput::dbus::Status new_phase) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    phase_ = new_phase;
  }
  dbus_->EmitStatusChanged(vinput::dbus::StatusToString(new_phase));
}

void DaemonRuntimeController::ResetToIdle() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    phase_ = vinput::dbus::Status::Idle;
    current_order_.reset();
    current_is_command_ = false;
    current_selected_text_.clear();
    current_recording_pcm_.clear();
    pending_chunk_pcm_.clear();
    current_sample_count_ = 0;
    current_input_gain_ = 1.0f;
    recording_started_at_.reset();
    first_non_silent_at_.reset();
    first_partial_logged_ = false;
  }
  dbus_->EmitStatusChanged(
      vinput::dbus::StatusToString(vinput::dbus::Status::Idle));
  vinput::debug::Log("phase -> idle\n");
}

void DaemonRuntimeController::WorkerMain() {
  while (worker_running_) {
    RecognitionOrder order;
    {
      std::unique_lock<std::mutex> lock(state_mutex_);
      worker_cv_.wait(lock, [&]() {
        return current_order_.has_value() || !worker_running_.load();
      });
      if (!worker_running_ && !current_order_.has_value()) {
        break;
      }
      order = std::move(*current_order_);
    }

    try {
      auto runtime_settings = LoadCoreConfig();
      NormalizeCoreConfig(&runtime_settings);

      if (order.session) {
        if (order.audio_delivery_mode ==
            vinput::daemon::asr::AudioDeliveryMode::Buffered) {
          // Apply peak normalization at the device boundary before inference.
          if (runtime_settings.asr.normalizeAudio && !order.pcm.empty()) {
            std::vector<float> float_samples(order.pcm.size());
            for (std::size_t i = 0; i < order.pcm.size(); ++i)
              float_samples[i] = static_cast<float>(order.pcm[i]) / 32768.0f;
            vinput::audio::PeakNormalize(float_samples);
            for (std::size_t i = 0; i < order.pcm.size(); ++i)
              order.pcm[i] = static_cast<int16_t>(
                  std::clamp(float_samples[i] * 32768.0f, -32768.0f, 32767.0f));
          }
          std::string push_error;
          if (!order.session->PushAudio(order.pcm, &push_error)) {
            throw std::runtime_error(push_error.empty()
                                         ? "Failed to push buffered audio."
                                         : push_error);
          }
        }

        std::string recognition_error;
        auto result =
            vinput::daemon::asr::RecognitionSessionManager::ConsumeEvents(
                &order.session, false, &recognition_error);
        if (!result.error.empty()) {
          fprintf(stderr, "vinput-daemon: recognition error: %s\n",
                  result.error.c_str());
          dbus_->EmitError(vinput::dbus::ClassifyErrorText(result.error));
        }
        if (!result.text.empty()) {
          order.recognized_text = std::move(result.text);
        }
        order.pcm.clear();
      }

      auto pipeline_result = pipeline_->Process(
          order, runtime_settings,
          [&]() { SetPhase(vinput::dbus::Status::Postprocessing); });
      for (const auto &error : pipeline_result.errors) {
        if (!error.raw_message.empty()) {
          fprintf(stderr, "vinput-daemon: processing error: %s\n",
                  error.raw_message.c_str());
        }
        dbus_->EmitError(error);
      }
      dbus_->EmitRecognitionResult(
          vinput::result::Serialize(pipeline_result.payload));
    } catch (const std::exception &e) {
      fprintf(stderr, "vinput-daemon: worker exception: %s\n", e.what());
      dbus_->EmitError(vinput::dbus::MakeRawError(e.what()));
    } catch (...) {
      fprintf(stderr, "vinput-daemon: worker unknown exception\n");
      dbus_->EmitError(vinput::dbus::MakeErrorInfo(
          vinput::dbus::kErrorCodeProcessingUnknown, {}, {},
          "Unknown error during processing"));
    }

    ResetToIdle();
  }
}

}  // namespace vinput::daemon::runtime
