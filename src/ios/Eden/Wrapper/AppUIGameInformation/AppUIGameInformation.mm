# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

//
//  AppUIGameInformation.mm - Sudachi
//  Created by Jarrod Norwell on 1/20/24.
//

#import "AppUIGameInformation.h"
#import "../DirectoryManager/DirectoryManager.h"
#import "../EmulationSession/EmulationSession.h"

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/patch_manager.h"
#include "core/loader/loader.h"
#include "core/loader/nro.h"
#include "frontend_common/yuzu_config.h"

struct GameMetadata {
    std::string title;
    u64 programId;
    std::string developer;
    std::string version;
    std::vector<u8> icon;
    bool isHomebrew;
};


class SdlConfig final : public Config {
public:
    explicit SdlConfig(std::optional<std::string> config_path);
    ~SdlConfig() override;

    void ReloadAllValues() override;
    void SaveAllValues() override;

protected:
    void ReadSdlValues();
    void ReadSdlPlayerValues(std::size_t player_index);
    void ReadSdlControlValues();
    void ReadHidbusValues() override;
    void ReadDebugControlValues() override;
    void ReadPathValues() override {}
    void ReadShortcutValues() override {}
    void ReadUIValues() override {}
    void ReadUIGamelistValues() override {}
    void ReadUILayoutValues() override {}
    void ReadMultiplayerValues() override {}

    void SaveSdlValues();
    void SaveSdlPlayerValues(std::size_t player_index);
    void SaveSdlControlValues();
    void SaveHidbusValues() override;
    void SaveDebugControlValues() override;
    void SavePathValues() override {}
    void SaveShortcutValues() override {}
    void SaveUIValues() override {}
    void SaveUIGamelistValues() override {}
    void SaveUILayoutValues() override {}
    void SaveMultiplayerValues() override {}

    std::vector<Settings::BasicSetting*>& FindRelevantList(Settings::Category category) override;

public:
    static const std::array<int, Settings::NativeButton::NumButtons> default_buttons;
    static const std::array<int, Settings::NativeMotion::NumMotions> default_motions;
    static const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> default_analogs;
    static const std::array<int, 2> default_stick_mod;
    static const std::array<int, 2> default_ringcon_analogs;
};


#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "common/logging/log.h"
#include "input_common/main.h"

const std::array<int, Settings::NativeButton::NumButtons> SdlConfig::default_buttons = {
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_T,
    SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_B,
};

const std::array<int, Settings::NativeMotion::NumMotions> SdlConfig::default_motions = {
    SDL_SCANCODE_7,
    SDL_SCANCODE_8,
};

const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> SdlConfig::default_analogs{
    {
        {
            SDL_SCANCODE_UP,
            SDL_SCANCODE_DOWN,
            SDL_SCANCODE_LEFT,
            SDL_SCANCODE_RIGHT,
        },
        {
            SDL_SCANCODE_I,
            SDL_SCANCODE_K,
            SDL_SCANCODE_J,
            SDL_SCANCODE_L,
        },
    }};

const std::array<int, 2> SdlConfig::default_stick_mod = {
    SDL_SCANCODE_D,
    0,
};

const std::array<int, 2> SdlConfig::default_ringcon_analogs{{
    0,
    0,
}};

SdlConfig::SdlConfig(const std::optional<std::string> config_path) {
    Initialize(config_path);
    ReadSdlValues();
    SaveSdlValues();
}

SdlConfig::~SdlConfig() {
    if (global) {
        SdlConfig::SaveAllValues();
    }
}

void SdlConfig::ReloadAllValues() {
    Reload();
    ReadSdlValues();
    SaveSdlValues();
}

void SdlConfig::SaveAllValues() {
    SaveValues();
    SaveSdlValues();
}

void SdlConfig::ReadSdlValues() {
    ReadSdlControlValues();
}

void SdlConfig::ReadSdlControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        ReadSdlPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    ReadDebugControlValues();
    ReadHidbusValues();

    EndGroup();
}

void SdlConfig::ReadSdlPlayerValues(const std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix.append("player_").append(ToString(player_index)).append("_");
    }

    auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig()) {
        const auto profile_name =
            ReadStringSetting(std::string(player_prefix).append("profile_name"));
        if (profile_name.empty()) {
            // Use the global input config
            player = Settings::values.players.GetValue(true)[player_index];
            player.profile_name = "";
            return;
        }
    }

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& player_buttons = player.buttons[i];

        player_buttons = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeButton::mapping[i]), default_param);
        if (player_buttons.empty()) {
            player_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& player_analogs = player.analogs[i];

        player_analogs = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]), default_param);
        if (player_analogs.empty()) {
            player_analogs = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        auto& player_motions = player.motions[i];

        player_motions = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeMotion::mapping[i]), default_param);
        if (player_motions.empty()) {
            player_motions = default_param;
        }
    }
}

void SdlConfig::ReadDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& debug_pad_buttons = Settings::values.debug_pad_buttons[i];
        debug_pad_buttons = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeButton::mapping[i]), default_param);
        if (debug_pad_buttons.empty()) {
            debug_pad_buttons = default_param;
        }
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& debug_pad_analogs = Settings::values.debug_pad_analogs[i];
        debug_pad_analogs = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]), default_param);
        if (debug_pad_analogs.empty()) {
            debug_pad_analogs = default_param;
        }
    }
}

void SdlConfig::ReadHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    auto& ringcon_analogs = Settings::values.ringcon_analogs;

    ringcon_analogs = ReadStringSetting(std::string("ring_controller"), default_param);
    if (ringcon_analogs.empty()) {
        ringcon_analogs = default_param;
    }
}

void SdlConfig::SaveSdlValues() {
    LOG_DEBUG(Config, "Saving SDL configuration values");
    SaveSdlControlValues();

    WriteToIni();
}

void SdlConfig::SaveSdlControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        SaveSdlPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    SaveDebugControlValues();
    SaveHidbusValues();

    EndGroup();
}

void SdlConfig::SaveSdlPlayerValues(const std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix = std::string("player_").append(ToString(player_index)).append("_");
    }

    const auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig() && player.profile_name.empty()) {
        // No custom profile selected
        return;
    }

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeButton::mapping[i]),
                           player.buttons[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]),
                           player.analogs[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeMotion::mapping[i]),
                           player.motions[i], std::make_optional(default_param));
    }
}

void SdlConfig::SaveDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeButton::mapping[i]),
                           Settings::values.debug_pad_buttons[i],
                           std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]),
                           Settings::values.debug_pad_analogs[i],
                           std::make_optional(default_param));
    }
}

void SdlConfig::SaveHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    WriteStringSetting(std::string("ring_controller"), Settings::values.ringcon_analogs,
                       std::make_optional(default_param));
}

std::vector<Settings::BasicSetting*>& SdlConfig::FindRelevantList(Settings::Category category) {
    return Settings::values.linkage.by_category[category];
}







std::unordered_map<std::string, GameMetadata> m_game_metadata_cache;

GameMetadata CacheGameMetadata(const std::string& path) {
    const auto file =
        Core::GetGameFileFromPath(EmulationSession::GetInstance().System().GetFilesystem(), path);
    auto loader = Loader::GetLoader(EmulationSession::GetInstance().System(), file, 0, 0);

    GameMetadata entry;
    loader->ReadTitle(entry.title);
    loader->ReadProgramId(entry.programId);
    loader->ReadIcon(entry.icon);

    const FileSys::PatchManager pm{
        entry.programId, EmulationSession::GetInstance().System().GetFileSystemController(),
        EmulationSession::GetInstance().System().GetContentProvider()};
    const auto control = pm.GetControlMetadata();

    if (control.first != nullptr) {
        entry.developer = control.first->GetDeveloperName();
        entry.version = control.first->GetVersionString();
    } else {
        FileSys::NACP nacp;
        if (loader->ReadControlData(nacp) == Loader::ResultStatus::Success) {
            entry.developer = nacp.GetDeveloperName();
        } else {
            entry.developer = "";
        }

        entry.version = "1.0.0";
    }

    if (loader->GetFileType() == Loader::FileType::NRO) {
        auto loader_nro = reinterpret_cast<Loader::AppLoader_NRO*>(loader.get());
        entry.isHomebrew = loader_nro->IsHomebrew();
    } else {
        entry.isHomebrew = false;
    }

    m_game_metadata_cache[path] = entry;

    return entry;
}

GameMetadata GameMetadata(const std::string& path, bool reload = false) {
    if (!EmulationSession::GetInstance().IsInitialized()) {
        Common::FS::SetAppDirectory(DirectoryManager::AppUIDirectory());

        EmulationSession::GetInstance().System().Initialize();
        EmulationSession::GetInstance().InitializeSystem(false);
    }

    if (reload) {
        return CacheGameMetadata(path);
    }

    if (auto search = m_game_metadata_cache.find(path); search != m_game_metadata_cache.end()) {
        return search->second;
    }

    return CacheGameMetadata(path);
}


@implementation AppUIInformation
-(AppUIInformation *) initWithDeveloper:(NSString *)developer iconData:(NSData *)iconData isHomebrew:(BOOL)isHomebrew programID:(uint64_t)programID
                                    title:(NSString *)title version:(NSString *)version {
    if (self = [super init]) {
        self.developer = developer;
        self.iconData = iconData;
        self.isHomebrew = isHomebrew;
        self.programID = programID;
        self.title = title;
        self.version = version;
    } return self;
}
@end

@implementation AppUIGameInformation
+(AppUIGameInformation *) sharedInstance {
    static AppUIGameInformation *sharedInstance = NULL;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}


-(AppUIInformation *) informationForGame:(NSURL *)url {
    auto gameMetadata = GameMetadata([url.path UTF8String]);

    return [[AppUIInformation alloc] initWithDeveloper:[NSString stringWithCString:gameMetadata.developer.c_str() encoding:NSUTF8StringEncoding]
                                                iconData:[NSData dataWithBytes:gameMetadata.icon.data() length:gameMetadata.icon.size()]
                                              isHomebrew:gameMetadata.isHomebrew programID:gameMetadata.programId
                                                   title:[NSString stringWithCString:gameMetadata.title.c_str() encoding:NSUTF8StringEncoding]
                                                 version:[NSString stringWithCString:gameMetadata.version.c_str() encoding:NSUTF8StringEncoding]];
}
@end
