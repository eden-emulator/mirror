// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include "common/assert.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/k_worker_task.h"
#include "core/hle/kernel/k_worker_task_manager.h"
#include "core/hle/kernel/kernel.h"

namespace Kernel {

KWorkerTask::KWorkerTask(KernelCore& kernel) : KSynchronizationObject{kernel} {}

void KWorkerTask::DoWorkerTask(KernelCore& kernel) {
    if (auto* const thread = this->DynamicCast<KThread*>(); thread != nullptr) {
        return thread->DoWorkerTaskImpl(kernel);
    } else {
        auto* const process = this->DynamicCast<KProcess*>();
        ASSERT(process != nullptr);

        return process->DoWorkerTaskImpl(kernel);
    }
}

KWorkerTaskManager::KWorkerTaskManager() {}

KWorkerTaskManager::~KWorkerTaskManager() {
    if (m_waiting_thread.joinable()) {
        m_waiting_thread.request_stop();
        m_task_cv.notify_one();
        m_waiting_thread.join();
    }
}

void KWorkerTaskManager::AddTask(KernelCore& kernel, WorkerType type, KWorkerTask* task) {
    ASSERT(type <= WorkerType::Count);
    kernel.WorkerTaskManager().AddTask(kernel, task);
}

void KWorkerTaskManager::AddTask(KernelCore& kernel, KWorkerTask* task) {
    KScopedSchedulerLock sl(kernel);

    // spawn thread on demand
    if (!m_waiting_thread.joinable()) {
        LOG_INFO(Kernel, "spawning KWorkerTaskManager thread");
        m_waiting_thread = std::jthread([&kernel, this](std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                KWorkerTask* t;
                {
                    std::unique_lock lk{m_task_mutex};
                    m_task_cv.wait(lk);
                    if (stop_token.stop_requested())
                        break;
                    t = m_task_queue.back();
                    m_task_queue.pop_back();
                }
                t->DoWorkerTask(kernel);
            }
        });
    }

    {
        std::scoped_lock lk{m_task_mutex};
        m_task_queue.emplace_back(task);
    }
    m_task_cv.notify_one();
}

} // namespace Kernel
