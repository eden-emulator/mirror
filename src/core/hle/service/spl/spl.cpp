// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/spl/spl.h"

namespace Service::SPL {

SPL::SPL(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:") {}
SPL::~SPL() = default;

SPL_MIG::SPL_MIG(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:mig") {}
SPL_MIG::~SPL_MIG() = default;

SPL_FS::SPL_FS(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:fs") {}
SPL_FS::~SPL_FS() = default;

SPL_SSL::SPL_SSL(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:ssl") {}
SPL_SSL::~SPL_SSL() = default;

SPL_ES::SPL_ES(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:es") {}
SPL_ES::~SPL_ES() = default;

SPL_MANU::SPL_MANU(Core::System& system_, std::shared_ptr<Module> module_) : Interface(system_, std::move(module_), "spl:manu") {}
SPL_MANU::~SPL_MANU() = default;

} // namespace Service::SPL
