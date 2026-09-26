# Subsystem: HLE

## HOS Kernel

In brief, the HOS kernel is a microkernel, all services and programs run in userspace, the primary way to do communication between these is via `HIPC` (not covered here); otherwise most of the primitives reside in the forms of syscalls invoked via `svc #imm`. The kernel supports both 32-bit and 64-bit programs, and has the capacity to use 32, 36 and 39 bits of address space for spawned processes. Most of the networking stack is based off FreeBSD's network stack.

The emulator implements the majority of the syscalls pertaining to the HOS kernel itself. When we talk about the HOS Kernel (in the context of the emulator) we are strictly speaking about the mechanisms from which syscalls are handled (and it's subsequent side effects, such as the page table book-keeping). The emulator at it's current state is unable to load a custom low-level kernel and do supervisor-level emulation.

Most programs in NX eventually invoke an `svc`, which, depending on it's immediate value, will go on to be dispatched into one of the specific syscall handlers.

These can be seen in [svc.cpp](/src/core/hle/kernel/svc.cpp). All of these correspond to syscalls which userspace programs may perform.

In turn, these syscalls create the mechanisms that allows programs to use CMIF/TIPC as their primary IPC form to contact other services/processes running on the system, the details of which will not be covered here, but you can consult the relevant [SwitchBrew article: 'HIPC'](https://switchbrew.org/wiki/HIPC).

From the point of view of the programs, no special devices (such as PCIE, Realtek drivers, Bluetooth or USB) has to be handled by the emulator; this is because most of the fun occurs in specialized services such as `usb:u` or `pcie` services. Which aren't emulated (yet).

Due to the nature of syscalls, many of them interact with memory. The emulated kernel has an internal tree-like structure, borrowed from FreeBSD's intrusive red-black tree; this is used to track and find mappings added or removed. Thus most of the process space is emulated in this way.

The kernel keeps it's own separate pagetable, in a traditional sense, each process has it's own pagetable, this is true for HOS as well.

Every process keeps it's own tracking of the following structures:
- Name (13 characters)
- 64-bit ID
- A handle table
- Exclusive monitor
- Threads
- Held locks
- Thread local pages
- A page table for each process

The emulator willingly restricts itself to only use 4 threads (to emulate 4 cores), this is because most existing applications do not benefit greatly from the added core count, and in fact can be detrimental due to extra contention. This translates equitatively to about 4 `ArmInterface` slots for each process, these are then redirected to whatever is the last `pc` of the last thread running on the core is meant to be; proceed to run it, then when returning (due to halt or interruption), proceed to reschedule the thread.

The scheduler as-is isn't 100% faithful to the original (for example the original is cooperative and not preemptive), and has great timing variance (especially due to the fact the emulator can run in systems with wildly different timings).

## Services

Consult [SwitchBrew](https://switchbrew.org/) for per-service methods, and implementation details.

All services are instatiated implicitly using `ServiceFramework`. To register a new interface or service, you can inherit from said class, providing additionally a template parameter that references `Self`, for example:

```c++
// Follows as:
// class MyInterface [final] : public ServiceFramework<MyInterface> { ... };
// [final] is optional, but should be used whenever there is no intention of this class itself being inherited.
class IFloatingRegistrationRequest final : public ServiceFramework<IFloatingRegistrationRequest> {
public:
    // ctor, you can pass extra parameters here (if so required)
    explicit IFloatingRegistrationRequest(Core::System& system_)
        : ServiceFramework{system_, "IFloatingRegistrationRequest"}
    {}

    // Must be placed after all methods are defined (or declared).
    // Define here your functions and methods, please order them.
    // Use FindRequestTipc for TIPC handlers.
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetSessionId"},
        FunctionInfo{12, nullptr, "GetAccountId"},
        FunctionInfo{13, nullptr, "GetLinkedNintendoAccountId"},
        FunctionInfo{14, nullptr, "GetNickname"},
        FunctionInfo{15, nullptr, "GetProfileImage"},
        FunctionInfo{16, nullptr, "GetProfileLargeImage", MakeVersionGate({18,0,0})},
        FunctionInfo{21, nullptr, "LoadIdTokenCache"},
        FunctionInfo{100, nullptr, "RegisterUser"},
        FunctionInfo{101, nullptr, "RegisterUserWithUid"},
        FunctionInfo{102, nullptr, "RegisterNetworkServiceAccountAsync", MakeVersionGate({4,0,0})},
        FunctionInfo{103, nullptr, "RegisterNetworkServiceAccountWithUidAsync", MakeVersionGate({4,0,0})},
        FunctionInfo{110, nullptr, "SetSystemProgramIdentification"},
        FunctionInfo{111, nullptr, "EnsureIdTokenCacheAsync"}
    );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};
```

Try to keep service structures local, that is, don't place them on header files if they're only going to be used by a specific service.

In each `.cpp` file that uses `D<...>/C<...>` CMIF wrapper helpers, remember to include the corresponding instancer, so you don't face linker errors:
```c++
#include "core/hle/service/cmif_serialization.h"
```
This will properly instatiate the corresponding wrappers and decompose the provided arguments in the wiring order.
