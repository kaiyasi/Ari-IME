// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaiyasi
#ifndef INPUTER_INPUTER_H
#define INPUTER_INPUTER_H

#include <fcitx-config/configuration.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "buffer.h"
#include "layout.h"

class InputerEngine;

// User-facing configuration, surfaced in fcitx5-configtool. Keyboard layout
// choices are backed by layout.cpp so key classification and chewing's KB type
// stay in sync.
FCITX_CONFIGURATION(
    InputerConfig,
    fcitx::OptionWithAnnotation<inputer::KeyboardLayout,
                                inputer::KeyboardLayoutI18NAnnotation>
        keyboardLayout{
        this, "KeyboardLayout", _("Keyboard layout"), inputer::KeyboardLayout::Default};
    fcitx::Option<bool> fullWidthPunctuation{
        this, "FullWidthPunctuation",
        _("Always use full-width Chinese punctuation without a modifier. Off keeps ordinary punctuation literal; the configured ChinesePunctuationShortcut plus a punctuation key produces its Chinese form temporarily."),
        false};
    fcitx::OptionWithAnnotation<
        inputer::ChinesePunctuationShortcut,
        inputer::ChinesePunctuationShortcutI18NAnnotation>
        chinesePunctuationShortcut{
        this, "ChinesePunctuationShortcut",
        _("Modifier used for temporary Chinese punctuation (default Ctrl+Shift). Choose Alt+Shift or another option if an application uses the default gesture."),
        inputer::ChinesePunctuationShortcut::ControlShift};
    fcitx::Option<bool> spaceCandidateMode{
        this, "SpaceCandidateMode",
        _("Use Space to open Chinese candidates after a complete syllable. Off keeps Ari's mixed-input Space-as-tone-one and literal-space behavior; Enter remains the commit key."),
        false};
    fcitx::Option<bool> autoLearn{
        this, "AutoLearn",
        _("Learn accepted Chinese choices locally. Turn this off to keep the personal dictionary unchanged; sensitive fields never learn regardless of this setting."),
        true};
    fcitx::Option<bool> showStatusLine{
        this, "ShowStatusLine",
        _("Show composition status text in the auxiliary line (for example 中 · 大千 · 半形標點) while composing."),
        false};
    fcitx::KeyListOption fullWidthPunctuationToggle{
        this, "FullWidthPunctuationToggle",
        _("Optional shortcut to toggle full-width punctuation on/off. Empty by default so no application shortcut is reserved; set for example Control+period. A modifier is required."),
        {}, fcitx::KeyListConstrain()};);

// Per-input-context state, owned by fcitx and created on demand.
class InputerState : public fcitx::InputContextProperty {
public:
    InputerState() = default;
    Buffer buffer;
    // Set once we have warned the user that the 注音 engine failed to load, so
    // the transient hint is not shown on every keystroke.
    bool engineErrorNotified = false;
};

class InputerEngine : public fcitx::InputMethodEngineV2 {
public:
    explicit InputerEngine(fcitx::Instance *instance);

    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &keyEvent) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;

    // --- Configuration (fcitx5-configtool) ---
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &config) override {
        config_.load(config, true);
        applyConfig();
        fcitx::safeSaveAsIni(config_, "conf/inputer.conf");
    }
    void reloadConfig() override {
        fcitx::readAsIni(config_, "conf/inputer.conf");
        applyConfig();
    }

private:
    inputer::KeyboardLayout applyConfig();
    void updateUI(fcitx::InputContext *ic, Buffer &buffer);
    void applyResult(fcitx::InputContext *ic, Buffer &buffer,
                     const KeyResult &result);
    // Current clipboard contents (Ctrl+V), or empty if the clipboard module is
    // unavailable. Loads the clipboard addon on demand.
    std::string clipboardText(fcitx::InputContext *ic);

    fcitx::Instance *instance_;
    fcitx::FactoryFor<InputerState> factory_;
    InputerConfig config_;
};

class InputerEngineFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new InputerEngine(manager->instance());
    }
};

#endif // INPUTER_INPUTER_H
