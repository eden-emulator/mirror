// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <thread>
#include "common/common_types.h"
#include "common/thread_worker.h"

namespace Kernel {

class KernelCore;
class KWorkerTask;

class KWorkerTaskManager final {
public:
    enum class WorkerType : u32 {
        Exit,
        Count,
    };

    KWorkerTaskManager(KernelCore& kernel);
    ~KWorkerTaskManager();

    static void AddTask(KernelCore& kernel, WorkerType type, KWorkerTask* task);
private:
    void AddTask(KernelCore& kernel, KWorkerTask* task);
    std::jthread m_waiting_thread;

    std::mutex m_task_mutex;
    std::condition_variable_any m_task_cv;
    std::vector<KWorkerTask*> m_task_queue;
};

} // namespace Kernel
