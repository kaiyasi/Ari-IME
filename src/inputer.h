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

class InputerEngine;

// User-facing configuration, surfaced in fcitx5-configtool. Note: the keyboard
// layout is currently 大千-only because the syllable classification in buffer.cpp
// (slotOf/canonical) is layout-specific; multi-layout support is a future task.
FCITX_CONFIGURATION(
    InputerConfig,
    fcitx::Option<bool> fullWidthPunctuation{
        this, "FullWidthPunctuation",
        _("Output full-width Chinese punctuation (，。？) for punctuation keys"),
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
        fcitx::safeSaveAsIni(config_, "conf/inputer.conf");
    }
    void reloadConfig() override {
        fcitx::readAsIni(config_, "conf/inputer.conf");
    }

private:
    void updateUI(fcitx::InputContext *ic, Buffer &buffer);

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
