#include "core/vinput.h"
#include "common/config/core_config.h"
#include "common/config/core_config_types.h"
#include "common/asr/model_manager.h"
#include "common/i18n.h"
#include "common/registry/registry_i18n.h"
#include "common/scene/postprocess_scene.h"

#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr int kMenuPageSize = 10;

struct SceneOption {
  std::size_t index;
  std::string label;
};

std::string SceneMenuTitle() { return _("Choose Postprocess Menu"); }

std::string AsrMenuTitle() { return _("Choose ASR Provider / Model"); }

std::string NormalizeSearchText(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

void PopLastUtf8Char(std::string *text) {
  if (!text || text->empty()) {
    return;
  }

  std::size_t pos = text->size();
  do {
    --pos;
  } while (pos > 0 &&
           (static_cast<unsigned char>((*text)[pos]) & 0xC0) == 0x80);
  text->erase(pos);
}

bool MatchesSearch(const AsrMenuItem &item, const std::string &query) {
  if (query.empty()) {
    return true;
  }
  return NormalizeSearchText(item.search_text).find(NormalizeSearchText(query)) !=
         std::string::npos;
}

std::string ResultMenuTitle(std::size_t count) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), _("Choose Result (%zu)"), count);
  return buf;
}

std::string ResultCandidateComment(const vinput::result::Candidate &candidate,
                                    std::size_t llm_index) {
  if (candidate.source == vinput::result::kSourceRaw) {
    return _("Original");
  }
  if (candidate.source == vinput::result::kSourceAsr) {
    return _("Voice Command");
  }
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%zu", llm_index);
  return buf;
}

std::string DisplayCandidateText(std::string text) {
  for (char &ch : text) {
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  return text;
}

std::string DecoratePagedMenuTitle(const std::string &base_title,
                                   fcitx::CandidateList *candidate_list) {
  auto *pageable = candidate_list ? candidate_list->toPageable() : nullptr;
  if (!pageable) {
    return base_title;
  }

  const int total_pages = pageable->totalPages();
  const int current_page = pageable->currentPage();
  if (total_pages <= 1 || current_page < 0) {
    return base_title;
  }

  char buf[64];
  std::snprintf(buf, sizeof(buf), _(" (%d/%d)"), current_page + 1, total_pages);
  return base_title + buf;
}

std::string DecorateFilterTitle(const std::string &base_title,
                                const std::string &query,
                                bool filter_mode) {
  if (!filter_mode && query.empty()) {
    return base_title;
  }
  return base_title + " / " + query;
}

void SetMenuTitle(fcitx::InputContext *ic, const std::string &base_title,
                  fcitx::CandidateList *candidate_list) {
  if (!ic) {
    return;
  }

  fcitx::Text aux_up;
  aux_up.append(DecoratePagedMenuTitle(base_title, candidate_list));
  ic->inputPanel().setAuxUp(aux_up);
}

void SetMenuTitle(fcitx::InputContext *ic, const std::string &base_title,
                  const std::string &query, bool filter_mode,
                  fcitx::CandidateList *candidate_list) {
  SetMenuTitle(ic, DecorateFilterTitle(base_title, query, filter_mode),
               candidate_list);
}

int DigitSelectionIndex(fcitx::CandidateList *candidate_list, int digit) {
  auto *pageable = candidate_list ? candidate_list->toPageable() : nullptr;
  int current_page = pageable ? pageable->currentPage() : 0;
  if (current_page < 0) {
    current_page = 0;
  }
  return current_page * kMenuPageSize + digit;
}

int CurrentSelectionIndex(fcitx::CandidateList *candidate_list) {
  if (!candidate_list) {
    return -1;
  }

  int current_index = candidate_list->cursorIndex();
  if (current_index < 0) {
    return -1;
  }

  auto *pageable = candidate_list->toPageable();
  int current_page = pageable ? pageable->currentPage() : 0;
  if (current_page < 0) {
    current_page = 0;
  }

  return current_page * kMenuPageSize + current_index;
}

void MoveCursorToIndex(fcitx::CandidateList *candidate_list, int target_index) {
  auto *cursor_list =
      candidate_list ? candidate_list->toCursorMovable() : nullptr;
  if (!cursor_list || target_index <= 0) {
    return;
  }

  for (int i = 0; i < target_index; ++i) {
    cursor_list->nextCandidate();
  }
}

std::string DisplayTextWithComment(std::string text,
                                   const std::string &comment) {
  if (comment.empty()) {
    return text;
  }
  text.append(" ");
  text.append(comment);
  return text;
}

bool ChangeCandidatePage(fcitx::InputContext *ic, const std::string &base_title,
                         bool next_page) {
  if (!ic) {
    return false;
  }

  auto candidate_list = ic->inputPanel().candidateList();
  auto *pageable = candidate_list ? candidate_list->toPageable() : nullptr;
  if (!pageable) {
    return false;
  }

  if (next_page) {
    if (!pageable->hasNext()) {
      return false;
    }
    pageable->next();
  } else {
    if (!pageable->hasPrev()) {
      return false;
    }
    pageable->prev();
  }

  SetMenuTitle(ic, base_title, candidate_list.get());
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  return true;
}

class SceneCandidateWord : public fcitx::CandidateWord {
public:
  SceneCandidateWord(VinputEngine *engine, SceneOption option, bool active)
      : fcitx::CandidateWord(fcitx::Text(DisplayTextWithComment(
            option.label, active ? _(" (Current)") : std::string()))),
        engine_(engine), index_(option.index) {}

  void select(fcitx::InputContext *inputContext) const override {
    engine_->selectScene(index_, inputContext);
  }

private:
  VinputEngine *engine_;
  std::size_t index_;
};

class AsrCandidateWord : public fcitx::CandidateWord {
public:
  AsrCandidateWord(VinputEngine *engine, std::size_t index,
                   const std::string &label, bool active)
      : fcitx::CandidateWord(fcitx::Text(DisplayTextWithComment(
            label, active ? _(" (Current)") : std::string()))),
        engine_(engine), index_(index) {}

  void select(fcitx::InputContext *inputContext) const override {
    engine_->selectAsrItem(index_, inputContext);
  }

private:
  VinputEngine *engine_;
  std::size_t index_;
};

class ResultCandidateWord : public fcitx::CandidateWord {
public:
  ResultCandidateWord(VinputEngine *engine, std::size_t index,
                      const std::string &text, const std::string &comment)
      : fcitx::CandidateWord(fcitx::Text(DisplayCandidateText(text))),
        engine_(engine), index_(index) {
    if (!comment.empty()) {
#ifdef VINPUT_FCITX5_CORE_HAVE_SET_COMMENT
      setComment(fcitx::Text(comment));
#endif
    }
  }

  void select(fcitx::InputContext *inputContext) const override {
    engine_->selectResultCandidate(index_, inputContext);
  }

private:
  VinputEngine *engine_;
  std::size_t index_;
};

} // namespace

void VinputEngine::showSceneMenu(fcitx::InputContext *ic) {
  if (!ic) {
    return;
  }

  reloadSceneConfig();
  scene_menu_ic_ = ic;
  scene_menu_visible_ = true;

  auto candidate_list = std::make_unique<fcitx::CommonCandidateList>();
  candidate_list->setPageSize(kMenuPageSize);
  candidate_list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candidate_list->setCursorPositionAfterPaging(
      fcitx::CursorPositionAfterPaging::ResetToFirst);

  int active_index = 0;
  for (std::size_t i = 0; i < scene_config_.scenes.size(); ++i) {
    const auto &scene = scene_config_.scenes[i];
    const bool active = scene.id == active_scene_id_;
    if (active) {
      active_index = static_cast<int>(i);
    }
    candidate_list->append<SceneCandidateWord>(
        this,
        SceneOption{
            .index = i,
            .label = vinput::scene::DisplayLabel(scene),
        },
        active);
  }
  MoveCursorToIndex(candidate_list.get(), active_index);

  SetMenuTitle(ic, SceneMenuTitle(), candidate_list.get());
  ic->inputPanel().setCandidateList(std::move(candidate_list));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void VinputEngine::hideSceneMenu() {
  if (!scene_menu_visible_ || !scene_menu_ic_) {
    scene_menu_visible_ = false;
    scene_menu_ic_ = nullptr;
    return;
  }

  scene_menu_visible_ = false;
  fcitx::Text empty;
  scene_menu_ic_->inputPanel().setAuxUp(empty);
  scene_menu_ic_->inputPanel().setCandidateList({});
  scene_menu_ic_->updateUserInterface(
      fcitx::UserInterfaceComponent::InputPanel);
  scene_menu_ic_ = nullptr;
}

bool VinputEngine::handleSceneMenuKeyEvent(fcitx::KeyEvent &keyEvent) {
  if (!scene_menu_visible_ || !scene_menu_ic_) {
    return false;
  }

  auto candidate_list = scene_menu_ic_->inputPanel().candidateList();
  auto *cursor_list =
      candidate_list ? candidate_list->toCursorMovable() : nullptr;
  if (keyEvent.isRelease()) {
    if (keyEvent.key().checkKeyList(scene_menu_key_) ||
        keyEvent.key().checkKeyList(page_prev_keys_) ||
        keyEvent.key().checkKeyList(page_next_keys_) ||
        keyEvent.key().digitSelection() >= 0 ||
        keyEvent.key().check(FcitxKey_Up) ||
        keyEvent.key().check(FcitxKey_Down) ||
        keyEvent.key().check(FcitxKey_Return) ||
        keyEvent.key().check(FcitxKey_KP_Enter) ||
        keyEvent.key().check(FcitxKey_Escape)) {
      keyEvent.filterAndAccept();
      return true;
    }
    return false;
  }

  if (keyEvent.key().checkKeyList(scene_menu_key_)) {
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_Escape)) {
    hideSceneMenu();
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().checkKeyList(page_prev_keys_)) {
    ChangeCandidatePage(scene_menu_ic_, SceneMenuTitle(), false);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().checkKeyList(page_next_keys_)) {
    ChangeCandidatePage(scene_menu_ic_, SceneMenuTitle(), true);
    keyEvent.filterAndAccept();
    return true;
  }

  const int digit = keyEvent.key().digitSelection();
  const int digit_index = DigitSelectionIndex(candidate_list.get(), digit);
  if (digit >= 0 &&
      digit_index < static_cast<int>(scene_config_.scenes.size())) {
    selectScene(static_cast<std::size_t>(digit_index), scene_menu_ic_);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Up)) {
    cursor_list->prevCandidate();
    scene_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Down)) {
    cursor_list->nextCandidate();
    scene_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_Return) ||
      keyEvent.key().check(FcitxKey_KP_Enter)) {
    int index = CurrentSelectionIndex(candidate_list.get());
    if (index < 0) {
      for (std::size_t i = 0; i < scene_config_.scenes.size(); ++i) {
        if (scene_config_.scenes[i].id == active_scene_id_) {
          index = static_cast<int>(i);
          break;
        }
      }
    }
    if (index >= 0 && index < static_cast<int>(scene_config_.scenes.size())) {
      selectScene(static_cast<std::size_t>(index), scene_menu_ic_);
    } else {
      hideSceneMenu();
    }
    keyEvent.filterAndAccept();
    return true;
  }

  hideSceneMenu();
  return false;
}

void VinputEngine::selectScene(std::size_t index, fcitx::InputContext *ic) {
  if (index >= scene_config_.scenes.size()) {
    hideSceneMenu();
    return;
  }

  const std::string selected_scene_id = scene_config_.scenes[index].id;
  // Persist the active scene to config
  auto core_config = LoadCoreConfig();
  core_config.scenes.activeScene = selected_scene_id;
  if (!SaveCoreConfig(core_config)) {
    notifyError(_("Failed to save active scene."));
    return;
  }
  active_scene_id_ = selected_scene_id;
  scene_config_.activeSceneId = selected_scene_id;
  hideSceneMenu();
  (void)ic;
}

void VinputEngine::reloadAsrMenuItems() {
  asr_menu_items_.clear();
  auto core_config = LoadCoreConfig();
  const std::string &active_provider = core_config.asr.activeProvider;
  const std::string active_model = ResolvePreferredLocalModel(core_config);
  const auto i18n_map = vinput::registry::FetchMergedI18nMap(
      core_config, vinput::registry::DetectPreferredLocale(), nullptr);

  for (const auto &provider : core_config.asr.providers) {
    const std::string &pid = AsrProviderId(provider);
    const std::string provider_title =
        vinput::registry::LookupI18n(i18n_map, pid + ".title", pid);
    if (std::holds_alternative<LocalAsrProvider>(provider)) {
      // Enumerate installed models under this local provider
      const auto base_dir = ResolveModelBaseDir(core_config);
      ModelManager manager(base_dir.string());
      auto models = manager.ListDetailed(active_model);
      for (const auto &summary : models) {
        const bool item_active =
            (pid == active_provider) && (summary.id == active_model);
        const std::string model_title = vinput::registry::LookupI18n(
            i18n_map, summary.id + ".title", summary.id);
        std::string label = model_title + " [local]";
        asr_menu_items_.push_back(AsrMenuItem{
            .provider_id = pid,
            .model_id = summary.id,
            .display_label = label,
            .search_text = label + " " + summary.id + " " + model_title + " " +
                           pid + " " + provider_title,
            .active = item_active,
        });
      }
    } else {
      // Command provider — one row
      const bool item_active = (pid == active_provider);
      std::string label = provider_title + " [command]";
      asr_menu_items_.push_back(AsrMenuItem{
          .provider_id = pid,
          .model_id = {},
          .display_label = label,
          .search_text = label + " " + pid + " " + provider_title,
          .active = item_active,
      });
    }
  }
}

void VinputEngine::rebuildAsrMenu(fcitx::InputContext *ic) {
  if (!ic) {
    return;
  }

  asr_menu_filtered_indices_.clear();
  for (std::size_t i = 0; i < asr_menu_items_.size(); ++i) {
    if (MatchesSearch(asr_menu_items_[i], asr_menu_query_)) {
      asr_menu_filtered_indices_.push_back(i);
    }
  }

  auto candidate_list = std::make_unique<fcitx::CommonCandidateList>();
  candidate_list->setPageSize(kMenuPageSize);
  candidate_list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candidate_list->setCursorPositionAfterPaging(
      fcitx::CursorPositionAfterPaging::ResetToFirst);

  int active_index = 0;
  for (std::size_t i = 0; i < asr_menu_filtered_indices_.size(); ++i) {
    const auto &item = asr_menu_items_[asr_menu_filtered_indices_[i]];
    if (item.active) {
      active_index = static_cast<int>(i);
    }
    candidate_list->append<AsrCandidateWord>(
        this, asr_menu_filtered_indices_[i], item.display_label, item.active);
  }
  MoveCursorToIndex(candidate_list.get(), active_index);

  SetMenuTitle(ic, AsrMenuTitle(), asr_menu_query_, asr_menu_filter_mode_,
               candidate_list.get());
  ic->inputPanel().setCandidateList(std::move(candidate_list));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void VinputEngine::showAsrMenu(fcitx::InputContext *ic) {
  if (!ic) {
    return;
  }

  reloadAsrMenuItems();
  asr_menu_ic_ = ic;
  asr_menu_visible_ = true;
  asr_menu_query_.clear();
  asr_menu_filter_mode_ = false;
  rebuildAsrMenu(ic);
}

void VinputEngine::hideAsrMenu() {
  if (!asr_menu_visible_ || !asr_menu_ic_) {
    asr_menu_visible_ = false;
    asr_menu_ic_ = nullptr;
    return;
  }

  asr_menu_visible_ = false;
  asr_menu_query_.clear();
  asr_menu_filter_mode_ = false;
  asr_menu_filtered_indices_.clear();
  fcitx::Text empty;
  asr_menu_ic_->inputPanel().setAuxUp(empty);
  asr_menu_ic_->inputPanel().setCandidateList({});
  asr_menu_ic_->updateUserInterface(
      fcitx::UserInterfaceComponent::InputPanel);
  asr_menu_ic_ = nullptr;
}

bool VinputEngine::handleAsrMenuKeyEvent(fcitx::KeyEvent &keyEvent) {
  if (!asr_menu_visible_ || !asr_menu_ic_) {
    return false;
  }

  auto candidate_list = asr_menu_ic_->inputPanel().candidateList();
  auto *cursor_list =
      candidate_list ? candidate_list->toCursorMovable() : nullptr;

  if (keyEvent.isRelease()) {
    if (keyEvent.key().checkKeyList(asr_menu_key_) ||
        keyEvent.key().checkKeyList(page_prev_keys_) ||
        keyEvent.key().checkKeyList(page_next_keys_) ||
        keyEvent.key().digitSelection() >= 0 ||
        keyEvent.key().check(FcitxKey_slash) ||
        keyEvent.key().check(FcitxKey_BackSpace) ||
        keyEvent.key().check(FcitxKey_Up) ||
        keyEvent.key().check(FcitxKey_Down) ||
        keyEvent.key().check(FcitxKey_Return) ||
        keyEvent.key().check(FcitxKey_KP_Enter) ||
        keyEvent.key().check(FcitxKey_Escape)) {
      keyEvent.filterAndAccept();
      return true;
    }
    if (asr_menu_filter_mode_ &&
        !fcitx::Key::keySymToUTF8(keyEvent.key().sym()).empty() &&
        !keyEvent.key().hasModifier()) {
      keyEvent.filterAndAccept();
      return true;
    }
    return false;
  }

  if (keyEvent.key().checkKeyList(asr_menu_key_)) {
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_Escape)) {
    if (asr_menu_filter_mode_ || !asr_menu_query_.empty()) {
      asr_menu_query_.clear();
      asr_menu_filter_mode_ = false;
      rebuildAsrMenu(asr_menu_ic_);
      keyEvent.filterAndAccept();
      return true;
    }
    hideAsrMenu();
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_slash)) {
    asr_menu_filter_mode_ = true;
    rebuildAsrMenu(asr_menu_ic_);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_BackSpace) && asr_menu_filter_mode_) {
    if (!asr_menu_query_.empty()) {
      PopLastUtf8Char(&asr_menu_query_);
    } else {
      asr_menu_filter_mode_ = false;
    }
    rebuildAsrMenu(asr_menu_ic_);
    keyEvent.filterAndAccept();
    return true;
  }

  if (asr_menu_filter_mode_) {
    const std::string utf8 = fcitx::Key::keySymToUTF8(keyEvent.key().sym());
    if (!utf8.empty() && !keyEvent.key().hasModifier()) {
      asr_menu_query_.append(utf8);
      rebuildAsrMenu(asr_menu_ic_);
      keyEvent.filterAndAccept();
      return true;
    }
  }

  if (keyEvent.key().checkKeyList(page_prev_keys_)) {
    ChangeCandidatePage(
        asr_menu_ic_,
        DecorateFilterTitle(AsrMenuTitle(), asr_menu_query_, asr_menu_filter_mode_),
        false);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().checkKeyList(page_next_keys_)) {
    ChangeCandidatePage(
        asr_menu_ic_,
        DecorateFilterTitle(AsrMenuTitle(), asr_menu_query_, asr_menu_filter_mode_),
        true);
    keyEvent.filterAndAccept();
    return true;
  }

  const int digit = keyEvent.key().digitSelection();
  const int digit_index = DigitSelectionIndex(candidate_list.get(), digit);
  if (digit >= 0 &&
      digit_index < static_cast<int>(asr_menu_items_.size())) {
    selectAsrItem(static_cast<std::size_t>(digit_index), asr_menu_ic_);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Up)) {
    cursor_list->prevCandidate();
    asr_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Down)) {
    cursor_list->nextCandidate();
    asr_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_Return) ||
      keyEvent.key().check(FcitxKey_KP_Enter)) {
    int index = CurrentSelectionIndex(candidate_list.get());
    if (index >= 0 && index < static_cast<int>(asr_menu_items_.size())) {
      selectAsrItem(static_cast<std::size_t>(index), asr_menu_ic_);
    } else {
      hideAsrMenu();
    }
    keyEvent.filterAndAccept();
    return true;
  }

  hideAsrMenu();
  return false;
}

void VinputEngine::selectAsrItem(std::size_t index, fcitx::InputContext *ic) {
  if (index >= asr_menu_items_.size()) {
    hideAsrMenu();
    return;
  }

  const AsrMenuItem &item = asr_menu_items_[index];
  auto core_config = LoadCoreConfig();
  core_config.asr.activeProvider = item.provider_id;
  if (!item.model_id.empty()) {
    std::string error;
    if (!SetPreferredLocalModel(&core_config, item.model_id, &error)) {
      notifyError(error);
      hideAsrMenu();
      return;
    }
  }
  if (!SaveCoreConfig(core_config)) {
    notifyError(_("Failed to save ASR config."));
    hideAsrMenu();
    return;
  }
  hideAsrMenu();
  restartDaemon();
  (void)ic;
}

void VinputEngine::showResultMenu(fcitx::InputContext *ic,
                                  const vinput::result::Payload &payload) {
  if (!ic || payload.candidates.empty()) {
    return;
  }

  hideSceneMenu();
  result_menu_ic_ = ic;
  result_menu_visible_ = true;
  result_candidates_ = payload.candidates;

  auto candidate_list = std::make_unique<fcitx::CommonCandidateList>();
  candidate_list->setPageSize(kMenuPageSize);
  candidate_list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  candidate_list->setCursorPositionAfterPaging(
      fcitx::CursorPositionAfterPaging::ResetToFirst);

  int cursor_index = 0;
  std::size_t llm_index = 0;
  for (std::size_t i = 0; i < result_candidates_.size(); ++i) {
    const auto &candidate = result_candidates_[i];
    if (candidate.source == vinput::result::kSourceLlm) {
      ++llm_index;
    }
    if (candidate.text == payload.commitText) {
      cursor_index = static_cast<int>(i);
    }
    candidate_list->append<ResultCandidateWord>(
        this, i, candidate.text, ResultCandidateComment(candidate, llm_index));
  }
  MoveCursorToIndex(candidate_list.get(), cursor_index);

  SetMenuTitle(ic, ResultMenuTitle(result_candidates_.size()),
               candidate_list.get());
  ic->inputPanel().setCandidateList(std::move(candidate_list));
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void VinputEngine::hideResultMenu() {
  if (!result_menu_visible_ || !result_menu_ic_) {
    result_menu_visible_ = false;
    result_menu_ic_ = nullptr;
    result_candidates_.clear();
    return;
  }

  result_menu_visible_ = false;
  fcitx::Text empty;
  result_menu_ic_->inputPanel().setAuxUp(empty);
  result_menu_ic_->inputPanel().setCandidateList({});
  result_menu_ic_->updateUserInterface(
      fcitx::UserInterfaceComponent::InputPanel);
  result_menu_ic_ = nullptr;
  result_candidates_.clear();
}

bool VinputEngine::handleResultMenuKeyEvent(fcitx::KeyEvent &keyEvent) {
  if (!result_menu_visible_ || !result_menu_ic_) {
    return false;
  }

  auto candidate_list = result_menu_ic_->inputPanel().candidateList();
  auto *cursor_list =
      candidate_list ? candidate_list->toCursorMovable() : nullptr;
  if (keyEvent.isRelease()) {
    if (keyEvent.key().digitSelection() >= 0 ||
        keyEvent.key().checkKeyList(page_prev_keys_) ||
        keyEvent.key().checkKeyList(page_next_keys_) ||
        keyEvent.key().check(FcitxKey_Up) ||
        keyEvent.key().check(FcitxKey_Down) ||
        keyEvent.key().check(FcitxKey_Return) ||
        keyEvent.key().check(FcitxKey_KP_Enter) ||
        keyEvent.key().check(FcitxKey_Escape)) {
      keyEvent.filterAndAccept();
      return true;
    }
    return false;
  }

  if (keyEvent.key().check(FcitxKey_Escape)) {
    hideResultMenu();
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().checkKeyList(page_prev_keys_)) {
    ChangeCandidatePage(result_menu_ic_,
                        ResultMenuTitle(result_candidates_.size()), false);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().checkKeyList(page_next_keys_)) {
    ChangeCandidatePage(result_menu_ic_,
                        ResultMenuTitle(result_candidates_.size()), true);
    keyEvent.filterAndAccept();
    return true;
  }

  const int digit = keyEvent.key().digitSelection();
  const int digit_index = DigitSelectionIndex(candidate_list.get(), digit);
  if (digit >= 0 && digit_index < static_cast<int>(result_candidates_.size())) {
    selectResultCandidate(static_cast<std::size_t>(digit_index),
                          result_menu_ic_);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Up)) {
    cursor_list->prevCandidate();
    result_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (cursor_list && keyEvent.key().check(FcitxKey_Down)) {
    cursor_list->nextCandidate();
    result_menu_ic_->updateUserInterface(
        fcitx::UserInterfaceComponent::InputPanel);
    keyEvent.filterAndAccept();
    return true;
  }

  if (keyEvent.key().check(FcitxKey_Return) ||
      keyEvent.key().check(FcitxKey_KP_Enter)) {
    int index = CurrentSelectionIndex(candidate_list.get());
    if (index < 0) {
      index = 0;
    }
    if (index >= 0 && index < static_cast<int>(result_candidates_.size())) {
      selectResultCandidate(static_cast<std::size_t>(index), result_menu_ic_);
    } else {
      hideResultMenu();
    }
    keyEvent.filterAndAccept();
    return true;
  }

  hideResultMenu();
  return false;
}

void VinputEngine::selectResultCandidate(std::size_t index,
                                         fcitx::InputContext *ic) {
  if (index >= result_candidates_.size()) {
    hideResultMenu();
    result_is_command_ = false;
    return;
  }

  const auto &candidate = result_candidates_[index];
  const std::string text = candidate.text;
  const bool is_command_result = result_is_command_;
  hideResultMenu();
  result_is_command_ = false;
  if (!ic) {
    return;
  }

  if (candidate.source == vinput::result::kSourceCancel) {
    clearPreedit(ic);
    return;
  }

  if (!text.empty()) {
    // command 模式：先用 surrounding text 删除选中内容
    if (is_command_result) {
      auto &surrounding = ic->surroundingText();
      if (surrounding.isValid() && surrounding.cursor() != surrounding.anchor()) {
        int cursor = surrounding.cursor();
        int anchor = surrounding.anchor();
        int from = std::min(cursor, anchor);
        int len = std::abs(cursor - anchor);
        ic->deleteSurroundingText(from - cursor, len);
      }
    }
    clearPreedit(ic);
    ic->commitString(text);
  }
}
