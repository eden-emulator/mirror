Dynarmic
========

A dynamic recompiler for ARM.

*Note that an adversarial guest program [can determine if it's being ran under Dynarmic](#disadvantages-of-dynarmic). Preventing this is not a goal of this project.*

Cortex-A57 (32 and 64 bit) is the emulated guest target. See [ArchVersion](../../src/dynarmic/src/dynarmic/interface/A32/arch_version.h).

The only supported host architectures are x86-64, and AArch64. There are no plans to support any 32-bit architectures.

See [an example usage](../../src/dynarmic/tests/print_info.cpp).

Design documentation can be found at [./Design.md](./Design.md).

Alternatives to Dynarmic
------------------------

Here are some projects with the same goals as Dynarmic:

* [Unicorn](https://www.unicorn-engine.org/) - Recompiling multi-architecture CPU emulator, based on QEMU
* [SkyEye](http://skyeye.sourceforge.net) - Cached interpreter for ARM

More general alternatives:

* [tARMac](https://davidsharp.com/tarmac/) - Tarmac's use of armlets was initial inspiration for us to use an intermediate representation
* [QEMU](https://www.qemu.org/) - Recompiling multi-architecture system emulator
* [VisUAL](https://salmanarif.bitbucket.io/visual/index.html) - Visual ARM UAL emulator intended for education
* A wide variety of other recompilers, interpreters and emulators can be found embedded in other projects, here are some we would recommend looking at:
  * [firebird's recompiler](https://github.com/nspire-emus/firebird) - Takes more of a call-threaded approach to recompilation
  * [higan's arm7tdmi emulator](https://github.com/higan-emu/higan/tree/master/higan/component/processor/arm7tdmi) - Very clean code-style
  * [arm-js by ozaki-r](https://github.com/ozaki-r/arm-js) - Emulates ARMv7A and some peripherals of Versatile Express, in the browser

Disadvantages of Dynarmic
-------------------------

In the pursuit of speed, some behavior not commonly depended upon is elided. Hence this emulator doesn't match spec.
Note that this would mean that a guest application can easily determine if it's being ran under instrumentation.

Known examples:

* Only user-mode is emulated, there is no emulation of any other privilege levels.
* FPSR state is approximate.
* Misaligned loads/stores are not appropriately trapped in certain cases.
* Exclusive monitor behavior may not match any known physical processor.

No formal verification has been done, and no security assessment has been made.
Use this code base at your own risk.

Legal
-----

Dynarmic is under a GPLv3 license, check the relevant file headers for more information.

Dynarmic uses several open-source libraries, whose licenses are included inside thereof.
