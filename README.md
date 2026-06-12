# fNDA — Free NVIDIA Driver for Apple

**Work In Progress — not tested on bare metal. Looking for testers!**

fNDA is an open-source macOS IOKit kernel extension (kext) for NVIDIA Ampere (GA10x) GPUs. It is a byte-for-byte port of NVIDIA's open-gpu-kernel-modules to macOS IOKit framework.

## Hardware Support

| GPU | Status |
|-----|--------|
| GA104 (RTX 3070 Ti) | ✅ Development target |
| GA102 (RTX 3080/3090) | ❌ Untested, likely needs minor changes |
| GA106 (RTX 3060) | ❌ Untested |
| GA107 (RTX 3050/3060 Ti) | ❌ Untested |

## What Works

- ✅ GA104Device registers in IORegistry
- ✅ PCI BAR0/BAR1/BAR2 mapping
- ✅ GSP firmware loading via userspace (UserClient)
- ✅ GSP-RM communication (rPtr > 0)
- ✅ GA104FBProvider — IOKit framebuffer provider nub
- ✅ GA104Framebuffer — IOFramebuffer implementation (stub)
- ✅ Legacy display init — VPLL, SOR, head programming
- ✅ programHeadForMode() — raster timing setup

## What Doesn't Work (Yet)

- ❌ Real display output (setDisplayMode needs proper EDID/DP link training)
- ❌ DP/HDMI link training
- ❌ NVKMS flip/modesetting
- ❌ RM memory management
- ❌ Graphics acceleration (Vulkan/Metal)

## Project Structure

```
fNDA/
├── Src/                    # IOKit kernel extension source
│   ├── GA104Device.cpp     # Main IOKit driver (GSP-RM, BARs)
│   ├── GA104Device.hpp
│   ├── GA104FBProvider.cpp # Framebuffer provider nub
│   ├── GA104FBProvider.hpp
│   ├── GA104Framebuffer.cpp# IOFramebuffer implementation
│   ├── GA104Framebuffer.hpp
│   ├── GA104UserClient.cpp # Userspace communication
│   ├── GA104UserClient.hpp
│   ├── GA104Regs.h         # Full register definitions (GA10x)
│   ├── GSPFirmware.cpp     # GSP firmware loader
│   ├── GSPFirmware.hpp
│   ├── GSPFirmwareParser.cpp
│   ├── GSPFirmwareParser.hpp
│   ├── GSPQueue.cpp        # GSP command/message queues
│   ├── GSPQueue.hpp
│   ├── GSPProtocol.hpp     # GSP RPC protocol structures
│   ├── GSPProtocol.cpp
│   ├── VBIOSDisplay.cpp    # VBIOS parser (connectors, timings)
│   ├── VBIOSDisplay.hpp
│   ├── KmodInfo.cpp
│   └── Makefile
├── tools/                  # VM management scripts
├── Resources/              # GSP firmware, VBIOS
├── Info.plist              # Kext bundle metadata
├── README.md
├── COMPILING.md
├── COMPATIBILITY.md
├── KNOWN_ISSUES.md
└── LICENSE
```

## Porting Approach

The driver is a **direct port** of NVIDIA's open-gpu-kernel-modules:

| Linux Source | macOS Port | Status |
|-------------|-----------|--------|
| `src/nvidia/src/kernel/` | GA104Device (GSP-RM, PCI) | ✅ |
| `nvidia-modeset/src/nvkms-evo*.c` | GA104Device (display init) | ✅ |
| `nvidia-modeset/src/nvkms-dpy.c` | GA104FBProvider | ✅ |
| `nvidia-modeset/include/nvkms-types.h` | GA104Framebuffer | ⚠️ Stub |
| `kernel-open/nvidia-modeset/` | UserClient | ✅ |

**Key compatibility layer:** `os_compat.h` maps Linux kernel APIs to macOS IOKit.

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE).

Based on [NVIDIA open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules) (GPL v2 / MIT).
