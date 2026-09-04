// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cmath>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_effect.h"
#include "yuzu/configuration/configure_post_processing.h"

namespace {

std::array<float, 4> CurrentValue(int index, const VideoCore::FxUniformDesc& uniform) {
    auto& chain = VideoCore::FxChain::Instance();
    if (chain.HasValue(static_cast<size_t>(index), uniform.name)) {
        return chain.GetValue(static_cast<size_t>(index), uniform.name);
    }
    return uniform.default_value;
}

int SliderSteps(const VideoCore::FxUniformDesc& uniform) {
    const float span = uniform.ui_max - uniform.ui_min;
    const int steps = static_cast<int>(std::lround(span / uniform.ui_step));
    if (steps < 1) {
        return 1;
    }
    return steps;
}

QString FormatValue(const VideoCore::FxUniformDesc& uniform, float value) {
    if (uniform.kind == VideoCore::FxUniformKind::Floating) {
        return QString::number(value, 'f', 3);
    }
    return QString::number(static_cast<int>(std::lround(value)));
}

QString SlotLabel(const VideoCore::FxEffectDesc& effect, const std::string& technique) {
    const QString name = QString::fromStdString(effect.name);
    if (effect.techniques.size() == 1) {
        return name;
    }
    return name + QStringLiteral(" · ") + QString::fromStdString(technique);
}

} // Anonymous namespace

ConfigurePostProcessing::ConfigurePostProcessing(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Post-Processing Effects"));
    setMinimumWidth(560);
    setMinimumHeight(460);

    auto* root = new QVBoxLayout(this);

    auto* description = new QLabel(
        tr("ReShade FX effects are loaded from the post_shaders folder in the Eden data "
           "directory. Changes apply immediately while a game is running."),
        this);
    description->setWordWrap(true);
    root->addWidget(description);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    slots_container = new QWidget(scroll);
    slots_layout = new QVBoxLayout(slots_container);
    slots_layout->setAlignment(Qt::AlignTop);
    scroll->setWidget(slots_container);
    root->addWidget(scroll, 1);

    auto* actions = new QHBoxLayout();

    auto* add_button = new QPushButton(tr("Add Effect"), this);
    connect(add_button, &QPushButton::clicked, this, [this]() {
        for (const auto& effect : VideoCore::GetFxCatalog()) {
            if (!effect.Valid()) {
                continue;
            }
            VideoCore::FxChain::Instance().Append(effect.file, effect.techniques.front());
            ApplyStructuralChange();
            return;
        }
    });
    actions->addWidget(add_button);

    auto* reload_button = new QPushButton(tr("Reload From Disk"), this);
    connect(reload_button, &QPushButton::clicked, this, [this]() {
        VideoCore::ReloadFxCatalog();
        VideoCore::FxChain::Instance().DropUnknownEntries();
        ApplyStructuralChange();
    });
    actions->addWidget(reload_button);

    auto* open_button = new QPushButton(tr("Open Folder"), this);
    connect(open_button, &QPushButton::clicked, this, []() {
        const auto path = VideoCore::GetFxRootDirectory();
        void(Common::FS::CreateDirs(path));
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(Common::FS::PathToUTF8String(path))));
    });
    actions->addWidget(open_button);

    actions->addStretch();
    root->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    VideoCore::ReloadFxCatalog();
    VideoCore::FxChain::Instance().DropUnknownEntries();
    RebuildRows();
}

ConfigurePostProcessing::~ConfigurePostProcessing() = default;

void ConfigurePostProcessing::ApplyStructuralChange() {
    VideoCore::FxChain::Instance().StoreToSettings();
    RebuildRows();
}

void ConfigurePostProcessing::PopulateEffectCombo(QComboBox* combo,
                                                  const VideoCore::FxChainEntry& entry) const {
    combo->clear();
    int selected = -1;

    for (const auto& effect : VideoCore::GetFxCatalog()) {
        if (!effect.Valid()) {
            continue;
        }
        for (const auto& technique : effect.techniques) {
            const QString key = QString::fromStdString(effect.file + "|" + technique);
            combo->addItem(SlotLabel(effect, technique), key);
            if (effect.file == entry.file && technique == entry.technique) {
                selected = combo->count() - 1;
            }
        }
    }

    if (selected >= 0) {
        combo->setCurrentIndex(selected);
    }
}

void ConfigurePostProcessing::BuildUniformWidget(QWidget* parent, QVBoxLayout* layout, int index,
                                                 const VideoCore::FxUniformDesc& uniform) {
    const auto value = CurrentValue(index, uniform);
    const QString label = QString::fromStdString(uniform.label);

    if (uniform.ui_type == VideoCore::FxUiType::CheckBox) {
        auto* box = new QCheckBox(label, parent);
        box->setChecked(value[0] != 0.0f);
        if (!uniform.tooltip.empty()) {
            box->setToolTip(QString::fromStdString(uniform.tooltip));
        }
        const std::string name = uniform.name;
        connect(box, &QCheckBox::toggled, this, [index, name](bool checked) {
            std::array<float, 4> next{};
            if (checked) {
                next[0] = 1.0f;
            }
            VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
            VideoCore::FxChain::Instance().StoreToSettings();
        });
        layout->addWidget(box);
        return;
    }

    if (uniform.ui_type == VideoCore::FxUiType::Combo ||
        uniform.ui_type == VideoCore::FxUiType::Radio) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label, parent));
        auto* combo = new QComboBox(parent);
        for (size_t i = 0; i < uniform.items.size(); ++i) {
            combo->addItem(QString::fromStdString(uniform.items[i]), static_cast<int>(i));
        }
        if (combo->count() == 0) {
            combo->addItem(tr("Enabled"), 1);
            combo->addItem(tr("Disabled"), 0);
        }
        const int current = static_cast<int>(std::lround(value[0]));
        if (current >= 0 && current < combo->count()) {
            combo->setCurrentIndex(current);
        }
        if (!uniform.tooltip.empty()) {
            combo->setToolTip(QString::fromStdString(uniform.tooltip));
        }
        const std::string name = uniform.name;
        connect(combo, &QComboBox::currentIndexChanged, this, [index, name](int selected) {
            std::array<float, 4> next{};
            next[0] = static_cast<float>(selected);
            VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
            VideoCore::FxChain::Instance().StoreToSettings();
        });
        row->addWidget(combo, 1);
        layout->addLayout(row);
        return;
    }

    auto* grid = new QGridLayout();
    for (unsigned component = 0; component < uniform.components; ++component) {
        QString component_label = label;
        if (uniform.components > 1) {
            component_label = label + QStringLiteral(" [%1]").arg(component);
        }

        auto* name_label = new QLabel(component_label, parent);
        auto* value_label = new QLabel(parent);
        value_label->setMinimumWidth(64);
        value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_label->setText(FormatValue(uniform, value[component]));

        auto* slider = new QSlider(Qt::Horizontal, parent);
        slider->setMinimum(0);
        slider->setMaximum(SliderSteps(uniform));
        slider->setValue(
            static_cast<int>(std::lround((value[component] - uniform.ui_min) / uniform.ui_step)));
        if (!uniform.tooltip.empty()) {
            slider->setToolTip(QString::fromStdString(uniform.tooltip));
        }

        const std::string name = uniform.name;
        const auto desc = uniform;
        connect(slider, &QSlider::valueChanged, this,
                [index, name, desc, component, value_label](int steps) {
                    auto next = CurrentValue(index, desc);
                    next[component] = desc.ui_min + static_cast<float>(steps) * desc.ui_step;
                    VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
                    VideoCore::FxChain::Instance().StoreToSettings();
                    value_label->setText(FormatValue(desc, next[component]));
                });

        grid->addWidget(name_label, static_cast<int>(component), 0);
        grid->addWidget(slider, static_cast<int>(component), 1);
        grid->addWidget(value_label, static_cast<int>(component), 2);
    }
    layout->addLayout(grid);
}

QWidget* ConfigurePostProcessing::BuildSlot(int index, const VideoCore::FxChainEntry& entry) {
    auto* group = new QGroupBox(slots_container);
    auto* layout = new QVBoxLayout(group);

    auto* header = new QHBoxLayout();

    auto* combo = new QComboBox(group);
    PopulateEffectCombo(combo, entry);
    connect(combo, &QComboBox::currentIndexChanged, this, [this, index, combo](int) {
        const QString key = combo->currentData().toString();
        const qsizetype separator = key.indexOf(QLatin1Char('|'));
        if (separator < 0) {
            return;
        }
        VideoCore::FxChain::Instance().Replace(static_cast<size_t>(index),
                                               key.left(separator).toStdString(),
                                               key.mid(separator + 1).toStdString());
        ApplyStructuralChange();
    });
    header->addWidget(combo, 1);

    auto* up_button = new QToolButton(group);
    up_button->setText(QStringLiteral("▲"));
    up_button->setEnabled(index > 0);
    connect(up_button, &QToolButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Move(static_cast<size_t>(index), -1);
        ApplyStructuralChange();
    });
    header->addWidget(up_button);

    auto* down_button = new QToolButton(group);
    down_button->setText(QStringLiteral("▼"));
    down_button->setEnabled(static_cast<size_t>(index) + 1 < VideoCore::FxChain::Instance().Size());
    connect(down_button, &QToolButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Move(static_cast<size_t>(index), 1);
        ApplyStructuralChange();
    });
    header->addWidget(down_button);

    auto* reset_button = new QToolButton(group);
    reset_button->setText(QStringLiteral("⟲"));
    reset_button->setToolTip(tr("Reset to defaults"));
    connect(reset_button, &QToolButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().ResetValues(static_cast<size_t>(index));
        ApplyStructuralChange();
    });
    header->addWidget(reset_button);

    auto* remove_button = new QToolButton(group);
    remove_button->setText(QStringLiteral("✕"));
    connect(remove_button, &QToolButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Remove(static_cast<size_t>(index));
        ApplyStructuralChange();
    });
    header->addWidget(remove_button);

    layout->addLayout(header);

    const VideoCore::FxEffectDesc* effect = VideoCore::FindFxEffect(entry.file);
    if (effect == nullptr) {
        auto* missing =
            new QLabel(tr("Effect '%1' was not found.").arg(QString::fromStdString(entry.file)),
                       group);
        missing->setWordWrap(true);
        layout->addWidget(missing);
        return group;
    }

    if (!effect->error.empty()) {
        auto* failed = new QLabel(
            tr("Effect failed to compile:\n%1").arg(QString::fromStdString(effect->error)), group);
        failed->setWordWrap(true);
        layout->addWidget(failed);
        return group;
    }

    std::string current_category;
    for (const auto& uniform : effect->uniforms) {
        if (uniform.category != current_category) {
            current_category = uniform.category;
            if (!current_category.empty()) {
                auto* category = new QLabel(QString::fromStdString(current_category), group);
                category->setStyleSheet(QStringLiteral("font-weight: bold;"));
                layout->addWidget(category);
            }
        }
        BuildUniformWidget(group, layout, index, uniform);
    }

    return group;
}

void ConfigurePostProcessing::RebuildRows() {
    QLayoutItem* item = nullptr;
    while ((item = slots_layout->takeAt(0)) != nullptr) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    bool has_usable = false;
    for (const auto& effect : VideoCore::GetFxCatalog()) {
        if (effect.Valid()) {
            has_usable = true;
            break;
        }
    }

    if (!has_usable) {
        auto* empty = new QLabel(
            tr("No usable ReShade FX effects were found. Place .fx files in the post_shaders "
               "folder."),
            slots_container);
        empty->setWordWrap(true);
        slots_layout->addWidget(empty);
        return;
    }

    const auto entries = VideoCore::FxChain::Instance().Entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        slots_layout->addWidget(BuildSlot(static_cast<int>(i), entries[i]));
    }
}
