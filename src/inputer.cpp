// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "inputer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

#include <clipboard_public.h>

#include "constants.h"
#include "unicode.h"

namespace {

// Display-only candidate; selection is driven from keyEvent so the visible
// highlight tracks our own ↑/↓ navigation.
class InputerCandidate : public fcitx::CandidateWord {
public:
    explicit InputerCandidate(
        const std::string &text, bool highlighted,
        std::function<void(fcitx::InputContext *)> onSelect)
        : onSelect_(std::move(onSelect)) {
        setText(fcitx::Text(text, highlighted
                                      ? fcitx::TextFormatFlag::HighLight
                                      : fcitx::TextFormatFlag::NoFlag));
    }
    void select(fcitx::InputContext *ic) const override {
        if (onSelect_) {
            onSelect_(ic);
        }
    }

private:
    std::function<void(fcitx::InputContext *)> onSelect_;
};

// Byte offset where the `n`-th grapheme cluster (0-based) starts.
std::size_t utf8Offset(const std::string &s, int n) {
    return inputer::unicode::graphemeOffset(s, n);
}

std::vector<std::string> utf8Chars(const std::string &s) {
    return inputer::unicode::splitGraphemes(s);
}

fcitx::Text buildEditingPreview(const std::string &text, int position,
                                bool picking) {
    constexpr int kContextChars = 10;
    const auto chars = utf8Chars(text);
    const int total = static_cast<int>(chars.size());
    position = std::clamp(position, 0, total);
    const int start = std::max(0, position - kContextChars);
    const int end = std::min(total, position + kContextChars + (picking ? 1 : 0));

    fcitx::Text preview;
    if (start > 0) {
        preview.append("…", fcitx::TextFormatFlag::NoFlag);
    }
    for (int i = start; i < end; ++i) {
        if (!picking && i == position) {
            preview.append("|", fcitx::TextFormatFlag::HighLight);
        }
        preview.append(chars[i], picking && i == position
                                     ? fcitx::TextFormatFlag::HighLight
                                     : fcitx::TextFormatFlag::NoFlag);
    }
    if (!picking && position == end) {
        preview.append("|", fcitx::TextFormatFlag::HighLight);
    }
    if (end < total) {
        preview.append("…", fcitx::TextFormatFlag::NoFlag);
    }
    return preview;
}

int utf8Count(const std::string &s) {
    return static_cast<int>(utf8Chars(s).size());
}

std::string joinAuxParts(const std::vector<std::string> &parts) {
    std::string out;
    for (const auto &part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " · ";
        }
        out += part;
    }
    return out;
}

void showInputerInformation(fcitx::Instance *instance, fcitx::InputContext *ic,
                            const std::string &message) {
#ifdef INPUTER_HAVE_CUSTOM_INPUT_METHOD_INFORMATION
    instance->showCustomInputMethodInformation(ic, message);
#else
    (void)message;
    instance->showInputMethodInformation(ic);
#endif
}

// Build the pre-edit text. The whole string is underlined to signal it is
// uncommitted. When a character is being selected (selChar >= 0) the caret is
// parked on it. We deliberately avoid per-segment highlight here: many clients
// ignore per-segment format flags and paint the whole pre-edit in one style, so
// a highlight would either vanish or bleed across everything. The caret, by
// contrast, is honoured almost universally and (together with the candidate
// window) clearly marks the character being edited.
fcitx::Text buildPreedit(const std::string &text, int caret) {
    fcitx::Text preedit;
    if (text.empty()) {
        return preedit;
    }
    preedit.append(text, fcitx::TextFormatFlag::Underline);
    int cursor = caret < 0
                     ? static_cast<int>(text.size())
                     : static_cast<int>(utf8Offset(text, caret));
    preedit.setCursor(cursor);
    return preedit;
}

std::string statusText(const Buffer &buffer) {
    std::string out = buffer.isForcedEnglish() ? "英" : "中";
    out += " · ";
    out += inputer::keyboardLayoutName(buffer.keyboardLayout());
    out += " · ";
    out += buffer.isFullWidthPunct() ? "全形標點" : "半形標點";
    return out;
}

bool isPasteShortcut(const fcitx::Key &key) {
    if (key.check(FcitxKey_v, fcitx::KeyState::Ctrl) ||
        key.check(FcitxKey_Insert, fcitx::KeyState::Shift)) {
        return true;
    }
    return key.sym() == FcitxKey_KP_Insert &&
           key.states().test(fcitx::KeyState::Shift);
}

void setPreedit(fcitx::InputContext *ic, fcitx::Text preedit) {
    if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
        ic->inputPanel().setClientPreedit(preedit);
    } else {
        ic->inputPanel().setPreedit(preedit);
    }
}

} // namespace

InputerEngine::InputerEngine(fcitx::Instance *instance)
    : instance_(instance),
      factory_([](fcitx::InputContext &) { return new InputerState(); }) {
    instance_->inputContextManager().registerProperty("inputerState", &factory_);
    reloadConfig();
}

inputer::KeyboardLayout InputerEngine::applyConfig() {
    inputer::KeyboardLayout layout = *config_.keyboardLayout;
    if (!inputer::keyboardLayoutAvailable(layout)) {
        layout = inputer::KeyboardLayout::Default;
    }
    inputer::setCurrentKeyboardLayout(layout);
    return layout;
}

void InputerEngine::updateUI(fcitx::InputContext *ic, Buffer &buffer) {
    auto &panel = ic->inputPanel();
    panel.reset();

    const std::string preeditStr = buffer.preeditText();
    const int selChar = buffer.selectionChar();
    const int caretChar = buffer.caretChar();
    // The caret follows the insertion point while typing mid-string (selChar is
    // -1 then); the aux-line highlight below still keys off selChar.
    setPreedit(ic, buildPreedit(preeditStr, caretChar));

    // The inline pre-edit above is rendered by the client application, which may
    // ignore per-segment styling (some paint the whole pre-edit reversed). So we
    // ALSO show the string in fcitx's own auxiliary line during selection, with
    // only the selected character highlighted — fcitx draws this with its own
    // theme, so the single-character mark is reliable regardless of the client.
    if (buffer.isEditing()) {
        const int position = buffer.isPicking() ? selChar : caretChar;
        panel.setAuxUp(
            buildEditingPreview(preeditStr, position, buffer.isPicking()));
    } else if (*config_.showPendingZhuyin) {
        // While an incomplete bopomofo syllable is pending, show its symbols
        // in Fcitx5's own auxiliary panel above the caret.
        const std::string hint = buffer.pendingSyllableHint();
        if (!hint.empty()) {
            panel.setAuxUp(fcitx::Text(hint));
        }
    }

    const int totalChars = utf8Count(preeditStr);
    std::string positionText;
    if (buffer.isEditing()) {
        if (buffer.isPicking()) {
            positionText = "選字 " + std::to_string(selChar + 1) + "/" +
                           std::to_string(totalChars);
        } else {
            positionText = "游標 " + std::to_string(std::max(caretChar, 0)) + "/" +
                           std::to_string(totalChars);
        }
    }

    auto candidates = buffer.candidates();
    if (!candidates.empty()) {
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        list->setPageSize(inputer::kCandPerPage);
        list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
        fcitx::KeyList selectionKeys;
        for (int i = 0; i < inputer::kCandPerPage; ++i) {
            selectionKeys.emplace_back(static_cast<fcitx::KeySym>(FcitxKey_1 + i));
        }
        list->setSelectionKey(selectionKeys);
        int hl = buffer.highlight();
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const std::string candidateText = candidates[i];
            list->append(std::make_unique<InputerCandidate>(
                candidateText, i == hl,
                [this, i, candidateText](fcitx::InputContext *ic) {
                    auto *state = ic->propertyFor(&factory_);
                    KeyResult result =
                        state->buffer.selectCandidate(i, candidateText);
                    applyResult(ic, state->buffer, result);
                }));
        }
        if (hl >= 0 && hl < static_cast<int>(candidates.size())) {
            list->setGlobalCursorIndex(hl);
        }
        panel.setCandidateList(std::move(list));

        int page = buffer.candidatePage();
        int pages = buffer.candidatePageCount();
        std::vector<std::string> auxParts;
        if (pages > 1) {
            auxParts.push_back("候選 " + std::to_string(page) + "/" +
                               std::to_string(pages));
        }
        auxParts.push_back(positionText);
        if (*config_.showStatusLine) {
            auxParts.push_back(statusText(buffer));
        }
        const std::string auxDown = joinAuxParts(auxParts);
        if (!auxDown.empty()) {
            panel.setAuxDown(fcitx::Text(auxDown));
        }
    } else {
        std::vector<std::string> auxParts{positionText};
        if (!preeditStr.empty() && *config_.showStatusLine) {
            auxParts.push_back(statusText(buffer));
        }
        const std::string auxDown = joinAuxParts(auxParts);
        if (!auxDown.empty()) {
            panel.setAuxDown(fcitx::Text(auxDown));
        }
    }

    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

std::string InputerEngine::clipboardText(fcitx::InputContext *ic) {
    auto *clipboard = instance_->addonManager().addon("clipboard", /*load=*/true);
    if (!clipboard) {
        return {}; // clipboard module unavailable: silently fall back
    }
    return clipboard->call<fcitx::IClipboard::clipboard>(ic);
}

bool InputerEngine::beginReconversion(fcitx::InputContext *ic,
                                      InputerState &state) {
    if (state.reconversionActive || state.buffer.isForcedEnglish() ||
        !state.buffer.preeditText().empty() ||
        !ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText) ||
        ic->capabilityFlags().test(fcitx::CapabilityFlag::PasswordOrSensitive)) {
        return false;
    }

    const auto &surrounding = ic->surroundingText();
    if (!surrounding.isValid() || surrounding.anchor() == surrounding.cursor()) {
        return false;
    }
    const unsigned int start =
        std::min(surrounding.anchor(), surrounding.cursor());
    const unsigned int end =
        std::max(surrounding.anchor(), surrounding.cursor());
    const unsigned int size = end - start;
    const int offset = static_cast<int>(start) -
                       static_cast<int>(surrounding.cursor());
    const std::string selected = surrounding.selectedText();
    if (selected.empty() || size == 0) {
        return false;
    }

    const KeyResult result = state.buffer.beginReconversion(selected);
    if (!result.handled) {
        return false;
    }

    // The selection is now represented by Ari's pre-edit. Delete the original
    // range from the client and update Fcitx's local surrounding-text cache so
    // Escape/reset can safely restore it at the same collapsed cursor.
    ic->deleteSurroundingText(offset, size);
    ic->surroundingText().deleteText(offset, size);
    state.reconversionActive = true;
    state.reconversionText = selected;
    return true;
}

void InputerEngine::applyResult(fcitx::InputContext *ic, Buffer &buffer,
                                const KeyResult &result) {
    if (result.hasCommit && !result.commitText.empty()) {
        ic->commitString(result.commitText);
    }
    if (result.notifyMode) {
        showInputerInformation(
            instance_,
            ic, buffer.isForcedEnglish() ? "英 English" : "中 中文");
    }
    if (!result.notification.empty()) {
        showInputerInformation(instance_, ic, result.notification);
    }
    if (result.updateUI) {
        updateUI(ic, buffer);
    }
}

void InputerEngine::keyEvent(const fcitx::InputMethodEntry &,
                             fcitx::KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }

    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->buffer.setLearningAllowed(
        *config_.autoLearn &&
        !ic->capabilityFlags().test(fcitx::CapabilityFlag::PasswordOrSensitive));

    // If the 注音 engine failed to initialise, warn once. Typing still works via
    // the buffer's plain-English degrade path, so we do not swallow the event.
    if (!state->buffer.engineReady() && !state->engineErrorNotified) {
        state->engineErrorNotified = true;
        showInputerInformation(
            instance_,
            ic, "注音引擎載入失敗，暫以英文輸入");
    }

    // Apply current config (cheap + idempotent) so toggling it in configtool
    // takes effect on every context without per-state bookkeeping.
    inputer::KeyboardLayout layout = applyConfig();
    bool layoutChanged = state->buffer.setKeyboardLayout(layout);
    bool punctChanged =
        state->buffer.setFullWidthPunct(*config_.fullWidthPunctuation);
    state->buffer.setChinesePunctuationShortcut(
        *config_.chinesePunctuationShortcut);
    state->buffer.setSpaceCandidateMode(*config_.spaceCandidateMode);
    if (layoutChanged) {
        std::string message =
            std::string("鍵盤 ") + inputer::keyboardLayoutName(layout);
        if (layout != *config_.keyboardLayout) {
            message += " (設定不可用，已退回)";
        }
        showInputerInformation(instance_, ic, message);
        updateUI(ic, state->buffer);
    }
    if (punctChanged) {
        showInputerInformation(
            instance_,
            ic, state->buffer.isFullWidthPunct() ? "標點 全形" : "標點 半形");
        updateUI(ic, state->buffer);
    }

    // Optional user-configured shortcut to toggle full-width punctuation. Empty
    // by default so no application shortcut is reserved; a modifier is required
    // by the config constraint, so this never shadows plain typing.
    if (!config_.fullWidthPunctuationToggle->empty() &&
        keyEvent.key().normalize().checkKeyList(
            *config_.fullWidthPunctuationToggle)) {
        bool on = !*config_.fullWidthPunctuation;
        config_.fullWidthPunctuation.setValue(on);
        fcitx::safeSaveAsIni(config_, "conf/inputer.conf");
        state->buffer.setFullWidthPunct(on);
        showInputerInformation(instance_, ic,
                               on ? "標點 全形" : "標點 半形");
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    // Ctrl+V / Shift+Insert paste clipboard text into our editable pre-edit
    // buffer. If the clipboard is empty/unavailable, let the application handle
    // the shortcut normally.
    const fcitx::Key &k = keyEvent.key();

    // Reopen a selected, short Chinese range for ordinary Ari candidate
    // editing. If the client does not expose a safe surrounding selection, the
    // shortcut is left untouched for the application.
    if (!config_.reconversionKey->empty() &&
        k.normalize().checkKeyList(*config_.reconversionKey) &&
        beginReconversion(ic, *state)) {
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    if (isPasteShortcut(k)) {
        std::string pasted = clipboardText(ic);
        if (!pasted.empty()) {
            state->buffer.pasteAtCaret(pasted);
            updateUI(ic, state->buffer);
            keyEvent.filterAndAccept();
            return;
        }
    }

    KeyResult result = state->buffer.handleKey(keyEvent.key());

    if (state->reconversionActive && k.sym() == FcitxKey_Escape &&
        result.handled) {
        state->buffer.reset();
        ic->commitString(state->reconversionText);
        state->reconversionActive = false;
        state->reconversionText.clear();
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    applyResult(ic, state->buffer, result);
    if (state->reconversionActive && result.hasCommit) {
        state->reconversionActive = false;
        state->reconversionText.clear();
    }
    if (result.handled) {
        keyEvent.filterAndAccept();
    }
}

void InputerEngine::reset(const fcitx::InputMethodEntry &,
                          fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    if (state->reconversionActive) {
        state->buffer.reset();
        ic->commitString(state->reconversionText);
        state->reconversionActive = false;
        state->reconversionText.clear();
    }
    state->buffer.reset();
    auto &panel = ic->inputPanel();
    panel.reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

FCITX_ADDON_FACTORY(InputerEngineFactory);
