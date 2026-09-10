// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QVBoxLayout;
class QWidget;

namespace VideoCore {
struct FxChainEntry;
struct FxEffectDesc;
struct FxUniformDesc;
}

class ConfigurePostProcessing : public QDialog {
    Q_OBJECT

public:
    explicit ConfigurePostProcessing(QWidget* parent = nullptr);
    ~ConfigurePostProcessing() override;

private:
    void RebuildRows();
    QWidget* BuildSlot(int index, const VideoCore::FxChainEntry& entry);
    void BuildUniformWidget(QWidget* parent, QVBoxLayout* layout, int index,
                            const VideoCore::FxUniformDesc& uniform);
    void PopulateEffectCombo(QComboBox* combo, const VideoCore::FxChainEntry& entry) const;
    void ApplyStructuralChange();
    void PopulatePresetCombo();
    void RefreshPresetStatus();

    QVBoxLayout* slots_layout{};
    QWidget* slots_container{};
    QComboBox* preset_combo{};
    QLabel* preset_status{};
    QCheckBox* enabled_box{};
};
