// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <objc/message.h>
#include <objc/runtime.h>

#include "common/logging.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/metal_common/metal_surface.h"

namespace Metal {

namespace {

/// The frontend hands us an `id` that it believes is a CAMetalLayer. On the Qt
/// path it may instead be a QContainerLayer when the CAMetalLayer sublayer
/// search failed (see qt_common.cpp), so verify rather than trust.
bool IsMetalLayer(void* handle) {
    Class metal_layer_class = objc_getClass("CAMetalLayer");
    if (metal_layer_class == nullptr) {
        return false;
    }
    using IsKindOfClassFn = bool (*)(id, SEL, Class);
    return reinterpret_cast<IsKindOfClassFn>(objc_msgSend)(
        static_cast<id>(handle), sel_registerName("isKindOfClass:"), metal_layer_class);
}

/// Best-effort class name, for the error message when the handle is wrong.
std::string ClassNameOf(void* handle) {
    if (handle == nullptr) {
        return "(null)";
    }
    const Class cls = object_getClass(static_cast<id>(handle));
    const char* name = cls != nullptr ? class_getName(cls) : nullptr;
    return name != nullptr ? std::string{name} : std::string{"(unknown)"};
}

/// Sets CALayer.opaque, which metal-cpp does not expose on CA::MetalLayer.
void SetLayerOpaque(CA::MetalLayer* layer) {
    using SetOpaqueFn = void (*)(id, SEL, BOOL);
    reinterpret_cast<SetOpaqueFn>(objc_msgSend)(reinterpret_cast<id>(layer),
                                                sel_registerName("setOpaque:"), YES);
}

} // Anonymous namespace

Owned<CA::MetalLayer> CreateSurface(
    const Device& device, const Core::Frontend::EmuWindow::WindowSystemInfo& window_info) {
    ScopedAutoreleasePool pool;

    if (window_info.type != Core::Frontend::WindowSystemType::Cocoa) {
        throw Exception("The Metal renderer requires a Cocoa/UIKit window surface");
    }

    void* handle = window_info.render_surface;
    if (handle == nullptr) {
        throw Exception("The frontend provided no render surface");
    }

    if (!IsMetalLayer(handle)) {
        throw Exception("Render surface is a " + ClassNameOf(handle) +
                        ", not a CAMetalLayer. The frontend must attach a "
                        "CAMetalLayer to the render view.");
    }

    // The frontend owns the layer and outlives us, but retaining is cheap and
    // removes any question about teardown ordering during a backend swap.
    Owned<CA::MetalLayer> layer = Retain(static_cast<CA::MetalLayer*>(handle));

    layer->setDevice(device.GetDevice());

    // CALayer defaults opaque to NO, which makes the compositor honour the
    // guest's framebuffer alpha as window transparency and renders the frame
    // see-through.
    SetLayerOpaque(layer.get());
    // Guaranteed present on every Metal device and matches what the guest
    // composites into.
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // The drawable is only ever rendered into and presented, never sampled or
    // read back, so Metal can skip the resolve/readback copy.
    layer->setFramebufferOnly(true);

    LOG_INFO(Render_Metal, "Bound CAMetalLayer to device {} (scale {:.2f})",
             device.GetDeviceName(), window_info.render_surface_scale);

    return layer;
}

} // namespace Metal
