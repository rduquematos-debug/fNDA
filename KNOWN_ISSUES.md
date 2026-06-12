# Known Issues

## Critical

### No Display Output
The IOFramebuffer `setDisplayMode()` is a stub. It calls `programHeadForMode()` which programs the display engine registers, but the WindowServer may not actually use this path. Real display requires:
- Proper EDID parsing via DDC/AUX
- DP/HDMI link training
- NVKMS flip modesetting integration

**Status:** ❌ Not working

### GSP Firmware Must Be Loaded from Userspace
The GSP firmware (~72MB) is NOT embedded in the kext. It must be loaded via UserClient methods:
- `sLoadFirmware` / `sFWAppendChunk` / `sFinalize`
- The firmware files (`gsp_firmware.bin`, `booter_load.bin`) are in `Resources/`

**Status:** ⚠️ Works but requires userspace tool

### rPtr > 0 Not Guaranteed on Bare Metal
The GSP-RM communication was tested only in QEMU/VFIO. Bare metal behavior is unknown.

**Status:** ❌ Untested

## Major

### IOFramebuffer Stub
Most IOFramebuffer methods return minimal values:
- Single display mode (1920x1080@60)
- No DP/HDMI link training
- No EDID reading
- No hotplug support

**Status:** ❌ Minimal stub only

### No Graphics Acceleration
The driver currently provides:
- PCI device identification ✅
- GSP-RM communication ✅
- Framebuffer registration (stub) ✅
- Display engine programming ✅

Not implemented:
- IOAccelerator / Metal support
- NVKMS flip/modesetting
- RM memory management
- DP link training / AUX channel

**Status:** ❌ Not implemented

### SEC2 Can't Halt in VFIO
The SEC2 engine boots but never reaches the halted state in QEMU/VFIO. This is expected — the RISC-V registers (0x111000+) are locked in VFIO.

**Status:** ⚠️ Expected behavior in VFIO, unknown on bare metal

## Minor

### GA104Regs.h Has Redundant Definitions
Some macros are defined multiple times (e.g., `NV01_ROOT`). This causes compiler warnings but doesn't affect functionality.

**Status:** ⚠️ Cosmetic

### OpenCore Config Schema Warnings
The config.plist may contain keys not recognized by your OpenCore version (`OCS: No schema for...`). These are harmless.

**Status:** ✅ Harmless
