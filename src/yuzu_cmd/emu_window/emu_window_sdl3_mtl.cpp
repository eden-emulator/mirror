// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <memory>
#include <string>

#include <format>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_mtl.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_properties.h>

EmuWindow_SDL3_MTL::EmuWindow_SDL3_MTL(InputCommon::InputSubsystem* input_subsystem_,
                                       Core::System& system_, bool fullscreen)
    : EmuWindow_SDL3{input_subsystem_, system_} {
    const std::string window_title = std::format("Eden {} | {}-{} (Metal)", std::string_view{Common::g_build_name},
                                                 std::string_view{Common::g_scm_branch}, std::string_view{Common::g_scm_desc});
    render_window = SDL_CreateWindow(window_title.c_str(), Layout::ScreenUndocked::Width,
                                     Layout::ScreenUndocked::Height,
                                     SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (render_window == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create a Metal window: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    SetWindowIcon();

    if (fullscreen) {
        Fullscreen();
        ShowCursor(false);
    }

    metal_view = SDL_Metal_CreateView(render_window);
    if (metal_view == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create a Metal view: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    void* metal_layer = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(metal_view));
    if (metal_layer == nullptr) {
        LOG_CRITICAL(Frontend, "Metal view has no CAMetalLayer: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    window_info.type = Core::Frontend::WindowSystemType::Cocoa;
    window_info.render_surface = metal_layer;

    OnResize();
    OnMinimalClientAreaChangeRequest(GetActiveConfig().min_client_area_size);
    SDL_PumpEvents();
    LOG_INFO(Frontend, "Eden Version: {} | {}-{} (Metal)", std::string_view{Common::g_build_name},
             std::string_view{Common::g_scm_branch}, std::string_view{Common::g_scm_desc});
}

EmuWindow_SDL3_MTL::~EmuWindow_SDL3_MTL() {
    if (metal_view != nullptr) {
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metal_view));
    }
}

std::unique_ptr<Core::Frontend::GraphicsContext> EmuWindow_SDL3_MTL::CreateSharedContext() const {
    return std::make_unique<DummyContext>();
}
