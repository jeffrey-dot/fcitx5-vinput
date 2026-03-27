#include "vinput.h"
#include "common/config/core_config.h"
#include "common/i18n.h"
#include "common/postprocess_scene.h"

#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>

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

void SetMenuTitle(fcitx::InputContext *ic, const std::string &base_title,
                  fcitx::CandidateList *candidate_list) {
  if (!ic) {
    return;
  }

  fcitx::Text aux_up;
  aux_up.append(DecoratePagedMenuTitle(base_title, candidate_list));
  ic->inputPanel().setAuxUp(aux_up);
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
