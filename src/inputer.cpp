// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#include "inputer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

#include <clipboard_public.h>

#include "constants.h"

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

// Byte length of the UTF-8 character starting at `s[i]`.
std::size_t utf8CharLen(const std::string &s, std::size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((c & 0x80) == 0x00) {
        len = 1;
    } else if ((c & 0xE0) == 0xC0) {
        len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
    }
    return std::min(len, s.size() - i);
}

// Byte offset where the `n`-th character (0-based) starts.
std::size_t utf8Offset(const std::string &s, int n) {
    std::size_t i = 0;
    for (int idx = 0; idx < n && i < s.size(); ++idx) {
        i += utf8CharLen(s, i);
    }
    return i;
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
    // The caret follows the insertion point while typing mid-string (selChar is
    // -1 then); the aux-line highlight below still keys off selChar.
    setPreedit(ic, buildPreedit(preeditStr, buffer.caretChar()));

    // The inline pre-edit above is rendered by the client application, which may
    // ignore per-segment styling (some paint the whole pre-edit reversed). So we
    // ALSO show the string in fcitx's own auxiliary line during selection, with
    // only the selected character highlighted — fcitx draws this with its own
    // theme, so the single-character mark is reliable regardless of the client.
    if (selChar >= 0) {
        fcitx::Text aux;
        int idx = 0;
        for (std::size_t i = 0; i < preeditStr.size();) {
            std::size_t len = utf8CharLen(preeditStr, i);
            aux.append(preeditStr.substr(i, len),
                       idx == selChar ? fcitx::TextFormatFlag::HighLight
                                      : fcitx::TextFormatFlag::NoFlag);
            i += len;
            ++idx;
        }
        panel.setAuxUp(aux);
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
            list->append(std::make_unique<InputerCandidate>(
                candidates[i], i == hl, [this, i](fcitx::InputContext *ic) {
                    auto *state = ic->propertyFor(&factory_);
                    KeyResult result = state->buffer.selectCandidate(i);
                    applyResult(ic, state->buffer, result);
                }));
        }
        if (hl >= 0 && hl < static_cast<int>(candidates.size())) {
            list->setGlobalCursorIndex(hl);
        }
        panel.setCandidateList(std::move(list));

        int page = buffer.candidatePage();
        int pages = buffer.candidatePageCount();
        std::string auxDown;
        if (pages > 1) {
            auxDown = "候選 " + std::to_string(page) + "/" +
                      std::to_string(pages) + " · ";
        }
        if (*config_.showStatusLine) {
            auxDown += statusText(buffer);
        } else if (pages > 1 && auxDown.size() >= 3) {
            auxDown.erase(auxDown.size() - 3); // drop the trailing " · "
        }
        if (!auxDown.empty()) {
            panel.setAuxDown(fcitx::Text(auxDown));
        }
    } else if (!preeditStr.empty() && *config_.showStatusLine) {
        panel.setAuxDown(fcitx::Text(statusText(buffer)));
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

void InputerEngine::applyResult(fcitx::InputContext *ic, Buffer &buffer,
                                const KeyResult &result) {
    if (result.hasCommit && !result.commitText.empty()) {
        ic->commitString(result.commitText);
    }
    if (result.notifyMode) {
        instance_->showCustomInputMethodInformation(
            ic, buffer.isForcedEnglish() ? "英 English" : "中 中文");
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

    // Apply current config (cheap + idempotent) so toggling it in configtool
    // takes effect on every context without per-state bookkeeping.
    inputer::KeyboardLayout layout = applyConfig();
    bool layoutChanged = state->buffer.setKeyboardLayout(layout);
    bool punctChanged =
        state->buffer.setFullWidthPunct(*config_.fullWidthPunctuation);
    if (layoutChanged) {
        std::string message =
            std::string("鍵盤 ") + inputer::keyboardLayoutName(layout);
        if (layout != *config_.keyboardLayout) {
            message += " (設定不可用，已退回)";
        }
        instance_->showCustomInputMethodInformation(ic, message);
        updateUI(ic, state->buffer);
    }
    if (punctChanged) {
        instance_->showCustomInputMethodInformation(
            ic, state->buffer.isFullWidthPunct() ? "標點 全形" : "標點 半形");
        updateUI(ic, state->buffer);
    }

    // Ctrl+V / Shift+Insert paste clipboard text into our editable pre-edit
    // buffer. If the clipboard is empty/unavailable, let the application handle
    // the shortcut normally.
    const fcitx::Key &k = keyEvent.key();
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

    applyResult(ic, state->buffer, result);
    if (result.handled) {
        keyEvent.filterAndAccept();
    }
}

void InputerEngine::reset(const fcitx::InputMethodEntry &,
                          fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->buffer.reset();
    auto &panel = ic->inputPanel();
    panel.reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

FCITX_ADDON_FACTORY(InputerEngineFactory);
