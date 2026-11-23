# Cross compiling

General guide for cross compiling.

## Debian ARM64

A painless guide for cross compilation (or to test NCE) from a x86_64 system without polluting your main.

```sh
#!/bin/sh
ISO_FILE="debian-13.0.0-arm64-netinst.iso"
DISK_FILE="debian-13-arm64-ci.qcow2"
which -- qemu-system-aarch64 || sudo pkg install qemu
[ -f "$ISO_FILE" ] || wget https://cdimage.debian.org/debian-cd/current/arm64/iso-cd/$ISO_FILE
[ -f "$DISK_FILE" ] || qemu-img create -f qcow2 "$DISK_FILE" 30G
QEMU_FLAGS=(
    -M virt
    -m 2G
    -cpu max
    -bios /usr/local/share/qemu/edk2-aarch64-code.fd
    -drive if=none,file=$ISO_FILE,format=raw,id=cdrom
    -drive if=none,file=$DISK_FILE,id=hd0,format=qcow2
    -device scsi-cd,drive=cdrom
    -device virtio-blk-device,drive=hd0
    -device virtio-gpu-pci
    -device usb-ehci
    -device usb-kbd
    -device intel-hda
    -device hda-output
    -nic user,model=virtio-net-pci
)
qemu-system-aarch64 "${QEMU_FLAGS[@]}"
```

## Gentoo

Gentoo's cross-compilation setup is relatively easy, provided you're already familiar with portage. A [cross toolchain file](../CMakeModules/toolchains/GentooCross.cmake) is provided. Throughout this section, replace `aarch64` with whatever target architecture you desire.

### Crossdev

First, emerge crossdev via `sudo emerge -a sys-devel/crossdev`.

Now, set up the environment depending on the target architecture; e.g.

```sh
sudo crossdev aarch64
```

### QEMU

If you don't have a host Gentoo system of your target architecture, you should install a QEMU user setup for testing. To do so, enable the relevant USE flags for `app-emulation/qemu`:

```txt
app-emulation/qemu static-user qemu_user_targets_aarch64
```

To use cross-emerged shared libraries, you will also need to tell qemu where the sysroot is. You can do this with an alias:

```sh
alias qemu-aarch64="qemu-aarch64 -L /usr/aarch64-unknown-linux-gnu"
```

### Dependencies

Dependencies are the same [as normal Gentoo](./Deps.md#Commands); simply replace the `emerge` command with `emerge-<target>-unknown-linux-gnu` (e.g. `emerge-aarch64-unknown-linux-gnu`). However, there are a few caveats:

#### Enabling GURU

Since Crossdev sysroots are effectively isolated from the system w.r.t. Portage, you must manually enable GURU in your sysroot. Run the following as root:

```sh
mkdir -p /usr/aarch64-unknown-linux-gnu/etc/portage/repos.conf
cat << EOF > /usr/aarch64-unknown-linux-gnu/etc/portage/repos.conf/guru.conf
[guru]
location = /var/db/repos/guru
auto-sync = no
priority = 1
EOF
```

#### Package Errata

Crossdev is not perfect, and you may face some challenges with packages that are not properly keyworded or have issues on specific architectures. These behaviors are, unfortunately, not well documented, and certain build systems such as Meson--and certain troublesome packages like GTK--are generally unfriendly towards cross-compilation.

Thus, it may be desirable to emerge a minimal set of dependencies and allow Eden's build system to handle the rest for you. At a minimum, you *only* need standard system libraries (Crossdev does this for you) and Qt:

```sh
sudo emerge-aarch64-unknown-linux-gnu dev-qt/qtbase:6 dev-qt/qtcharts:6
```

From here, CPMUtil will take care of everything else. For extra insurance, you may want to set `-DCPMUTIL_FORCE_BUNDLED=ON` in your configure command.

### Building

From here, building is relatively standard. The [cross toolchain file](../CMakeModules/toolchains/GentooCross.cmake) contains a few additional configurations, but generally all you need to do is set `CROSS_TARGET` and `CMAKE_TOOLCHAIN_FILE`. Disabling OpenGL is strongly recommended as well.

```sh
cmake -S . -B build/aarch64 -DCMAKE_TOOLCHAIN_FILE=CMakeModules/toolchains/GentooCross.cmake -GNinja -DCROSS_TARGET=aarch64 -DENABLE_OPENGL=OFF
```

With that done, you can build as normal:

```sh
cmake --build build/aarch64
```

And finally, run the compiled executable with QEMU!

```sh
qemu-aarch64 build/aarch64/bin/eden
```

## Windows from Linux

You can compile Eden for Windows from a Linux host in one of two ways.

### MinGW

This method is currently unsupported. Check back later.

### clang-cl (MSVC)

This compiles Eden in the same way that our native Windows MSVC builds are made.

First off, you *must* have a host Qt installed. If you want localization, install Qt Tools as well. Take note of where it's located; usually this will be `/usr/lib/qt`, or `/usr/lib64/qt6`, or something along those lines. You will also need a Clang 21+ build with `clang-cl` support included. Most distributions will have this by default.

Finally, set up an xwin root. You can set this up wherever; automated setup may come in the future. Download the latest [xwin](https://github.com/Jake-Shadle/xwin/releases/latest) for your architecture, then run:

```sh
/path/to/xwin --accept-license --arch x86_64 --manifest-version 18 \
  splat --preserve-ms-arch-notation --include-debug-libs --output xwin
```

A good place to put this would be in Eden's `.cache` directory.

Once all of those are setup, configure:

```sh
cmake -S . -B build/msvc -DXWIN_PATH=$PWD/.cache/xwin --toolchain CMakeModules/toolchains/MSVCCrossAMD64.cmake \
    -GNinja -DQT_HOST_PATH=/usr/lib64/qt6 -DCMAKE_BUILD_TYPE=Release
```

- `XWIN_PATH` is wherever you put the xwin splat.
- `QT_HOST_PATH` is the host installation of Qt 6 you noted earlier.
- This can be a bit slow during incremental builds, so `-DUSE_CCACHE=ON` is recommended.

From there, just build as normal:

```sh
cmake --build build/msvc
```

Your Windows executables will then be in `build/msvc/bin`. You can run these through WINE or transfer them to a Windows PC.

#### ARM

You can do the same with an ARM target:

```sh
/path/to/xwin --accept-license --arch aarch64 --manifest-version 18 \
  splat --preserve-ms-arch-notation --include-debug-libs --output xwin

cmake -S . -B build/msvc-arm -DXWIN_PATH=$PWD/.cache/xwin --toolchain CMakeModules/toolchains/MSVCCrossARM64.cmake \
    -GNinja -DQT_HOST_PATH=/usr/lib64/qt6 -DCMAKE_BUILD_TYPE=Release

cmake --build build/msvc-arm
```
