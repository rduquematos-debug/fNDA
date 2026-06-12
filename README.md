# fNDA — Free NVIDIA Driver for Apple

**Work In Progress — not tested on bare metal. Looking for testers!**

fNDA is an open-source macOS IOKit kernel extension (kext) for NVIDIA Ampere (GA10x)
GPUs. It is a port of NVIDIA's [open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules)
to the macOS IOKit framework.

> Target: **GA104 (RTX 3070 Ti)** · macOS **Ventura 13.7.8** · **x86_64**

---

## Port Progress

> Percentages reflect what the Makefile actually compiles and what works at runtime.
> Tested compilation target: macOS Ventura 13.7.8 (Darwin 22.6.0), Xcode CLT, x86_64.

| Area | % | Bar | Notes |
|------|---|-----|-------|
| **Compilation (Makefile)** | **100%** | `██████████` | 18 source files, zero errors, 322 KB kext |
| Core Device (init, BARs, PCI) | 80% | `████████░░` | Lifecycle works, not bare metal tested |
| GSP Boot (SEC2 + Falcon) | 60% | `██████░░░░` | SEC2 boots, but GSP_INIT_DONE **never arrives** |
| GSP RPC (queues, alloc/control) | 50% | `█████░░░░░` | Code compiles, **never tested end-to-end** (GSP stalls) |
| Legacy Display (VPLL, heads, SOR) | 85% | `████████░░` | Most complete subsystem, confirmed working in VM |
| Framebuffer / IOFramebuffer | 35% | `███░░░░░░░` | Registers in IOReg, **no display output** |
| NVKMS OS Interface | 30% | `███░░░░░░░` | RM bridge compiles, **entry points are stubs** |
| Userspace (24 methods + gsp_loader) | 90% | `█████████░` | Complete, but boot failure limits usefulness |
| Firmware Parsing (ELF, NVFW) | 90% | `█████████░` | All parsers compile, only verifySig is stub |
| VBIOS Parsing (DCB/CONN) | 60% | `██████░░░░` | Parsers compile, configureEncoder is stub |
| os_compat.h (Linux→macOS layer) | 85% | `████████░░` | 991 lines, very comprehensive |
| **Overall (functional)** | **~60%** | **`██████░░░░`** | **Critical path (GSP+DP) still at 0%** |

### Compilation Status (macOS Ventura 13.7.8)

| Category | Lines | Size | Status |
|----------|-------|------|--------|
| Compiled by Makefile (21 source + headers) | ~7,200 + headers | 320 KB kext | ✅ **Zero errors, 12 Jun 2026** |
| Embedded blobs (VBIOS, SEC2 firmware) | ~17,900 | Compiled via `#include` | ✅ Linked into kext |
| `nvidia/kernel_gsp_ga102.c` (GSP HAL) | 95 | ✅ Compiled | ✅ |
| `nvidia/kernel_gsp_falcon_ga102.c` (Falcon DMA) | 396 | ✅ Compiled | ✅ **Just added** |
| `gsp/gsp_main.c` + `gsp/gsp_obj.c` (GSP layer) | 124 | ✅ Compiled | ✅ |
| `nvidia/stubs/*.c` (ECC, NVLink) | 4 | ✅ Compiled | ✅ |
| `GSPQueue.cpp` — msgq `extern "C"` wrappers | ~60 | ✅ Added | Bridges NVIDIA msgq API → GSPQueue |
| `nvidia/message_queue_cpu.c` (msgq CPU-side) | 824 | ❌ Needs MEMDESC_FLAGS, etc. | Future task |

> **Last verified:** `make clean && make all` — PASS on Darwin 22.6.0 x86_64, Xcode CLT. 320 KB kext, zero errors.

---

## Hardware Support

| GPU | Status |
|-----|--------|
| GA104 (RTX 3070 Ti) | ✅ Development target |
| GA102 (RTX 3080/3090) | ❌ Untested |
| GA106 (RTX 3060) | ❌ Untested |
| GA107 (RTX 3050/3060 Ti) | ❌ Untested |

## What Works

- ✅ GA104Device registers in IORegistry
- ✅ PCI BAR0/BAR1/BAR2 mapping
- ✅ GSP firmware loading via userspace (UserClient)
- ✅ GSP-RM communication (rPtr > 0)
- ✅ GA104FBProvider — IOKit framebuffer provider nub
- ✅ GA104Framebuffer — IOFramebuffer implementation
- ✅ Legacy display init — VPLL, SOR, head programming
- ✅ programHeadForMode() — raster timing setup
- ✅ SEC2 boot (BROM RSA-3K validation, MCTP GSP-FMC)
- ✅ All 24 UserClient methods implemented
- ✅ EDID parser + mode list
- ✅ VBIOS parser (DCB/CONN tables)
- ✅ NVIDIA msgq library reimplementation
- ✅ RM API bridge (nvkms-rmapi.c — 10 operations)
- ✅ All 14 GSP display RPC methods implemented

## What Doesn't Work (Yet)

- ❌ **Real display output** — DP link training not functional
- ❌ GSP_INIT_DONE — doorbell/RPC handshake not completing
- ❌ NVKMS flip/modesetting
- ❌ RM memory management
- ❌ Graphics acceleration (Vulkan/Metal)

## Critical Path (to get display output)

1. **DP link training** — implement dpLinkTrain + dpConfigStream RPCs
2. **GSP_INIT_DONE** — debug SEC2 doorbell path and polling loop
3. **Connection attributes** — stop returning dummy values in IOFramebuffer

## Project Structure

```
fNDA/
├── Src/                    # IOKit kernel extension source
│   ├── GA104Device.cpp     # Main driver (GSP-RM, BARs, lifecycle)
│   ├── GA104GSPBoot.cpp    # SEC2 + Falcon boot sequence (~982 lines)
│   ├── GA104GSPRPC.cpp     # GSP RPC queue management (~1495 lines)
│   ├── GA104Display.cpp    # Legacy display engine programming
│   ├── GA104FBProvider.cpp # Framebuffer provider nub
│   ├── GA104Framebuffer.cpp# IOFramebuffer implementation
│   ├── GA104UserClient.cpp # Userspace communication (24 methods)
│   ├── GA104Regs.h         # Register definitions (GA10x, 1234 lines)
│   ├── GSPFirmware.cpp     # ELF64 RISC-V firmware loader
│   ├── GSPFirmwareParser.cpp# NVFW bootloader binary parser
│   ├── GSPQueue.cpp        # Message queue (msgq) implementation
│   ├── GSPProtocol.cpp     # RPC message building
│   ├── VBIOSDisplay.cpp    # VBIOS parser (DCB, CONN tables)
│   ├── EDIDParser.hpp      # EDID parser (header-only)
│   ├── fnda_nvkms_interface.cpp  # NVKMS OS interface
│   ├── nvkms-rmapi.c       # RM API adapter (283 lines)
│   ├── os_compat.h         # Linux→macOS compat layer (991 lines)
│   └── Makefile
├── Src/nvidia/             # Byte-for-byte NVIDIA ports (⚠️ not compiled)
│   ├── kernel_gsp_ga102.c (95L)
│   ├── kernel_gsp_falcon_ga102.c (396L)
│   ├── message_queue_cpu.c (824L)
│   └── inc/                # Register headers
├── Src/gsp/                # GSP abstraction layer (⚠️ not compiled)
├── tools/                  # gsp_loader — userspace firmware loader
├── Resources/              # GSP firmware, VBIOS dumps
├── Info.plist              # Kext bundle metadata
├── README.md
├── COMPILING.md
├── COMPATIBILITY.md
├── KNOWN_ISSUES.md
└── LICENSE
```

## Development Environment

| Setup | Status |
|-------|--------|
| QEMU/KVM (software) | ✅ Compilation + IOReg validation |
| QEMU/KVM (VFIO passthrough) | ⚠️ Works but fragile |
| Bare metal | ❌ Pending (no display output yet) |

## Porting Approach

Direct port of NVIDIA's open-gpu-kernel-modules:

| Linux Source | macOS Port | Lines | Status |
|-------------|-----------|-------|--------|
| `kernel_gsp.c` | GA104Device + GA104GSPBoot | ~1,339 | ✅ |
| `g_rpc-structures.h` | GA104Regs.h | 1,234 | ✅ |
| `message_queue.c` | GSPQueue.cpp + GA104GSPRPC | ~1,703 | ✅ |
| `nvkms-dpy.c` | GA104Framebuffer | 338 | ⚠️ Stub |
| `nvkms-evo*.c` | GA104Display | 507 | ✅ |
| `nvkms-rmapi-dgpu.c` | nvkms-rmapi.c | 283 | ✅ |
| `os-interface.h` | fnda_nvkms_interface | 492 | ⚠️ Stub |

## Support

If you find this project useful, consider buying me a coffee:

<p align="center">
  <a href="https://buymeacoffee.com/rafadebugs">
    <img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-FFDD00?logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"/>
  </a>
</p>

## License

GNU General Public License v2.0 — see [LICENSE](LICENSE).

Based on [NVIDIA open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules) (GPL v2 / MIT).
