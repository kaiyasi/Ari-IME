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
        _("Use full-width Chinese punctuation for punctuation keys (for example < -> ，, > -> 。, ? -> ？, _ -> ＿). Off by default; no global shortcut is reserved."),
        false};
    fcitx::Option<bool> showStatusLine{
        this, "ShowStatusLine",
        _("Show composition status text in the auxiliary line (for example 中 · 大千 · 半形標點) while composing."),
        false};);

// Per-input-context state, owned by fcitx and created on demand.
class InputerState : public fcitx::InputContextProperty {
public:
    InputerState() = default;
    Buffer buffer;
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
