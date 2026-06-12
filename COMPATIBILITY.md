# Compatibility

## Tested Hardware

| Hardware | Status | Notes |
|----------|--------|-------|
| **NVIDIA GA104 (RTX 3070 Ti)** | ✅ Development target | ASUS TUF Gaming variant |
| ASUS B450M DS3H | ✅ BIOS: CSM off, Above 4G on, IOMMU off | macOS Ventura |

## Tested macOS Versions

| Version | Build | Status |
|---------|-------|--------|
| Ventura 13.7.8 | 22H730 | ✅ Development target |
| Sonoma 14.x | — | ❌ Untested |
| Sequoia 15.x | — | ❌ Untested |

## Development Environment

| Setup | Status |
|-------|--------|
| QEMU/KVM (software) | ✅ Compilation + IOReg validation |
| QEMU/KVM (VFIO passthrough) | ⚠️ Works but fragile, VM corruption risk |
| Bare metal | ❌ Pending (no display output yet) |

## Known Working

- Kext loads in macOS 13.7.8 without kernel panic
- GA104Device registers in IOReg with BAR0/BAR1/BAR2 properties
- GSP firmware loading via UserClient (userspace)
- GSP-RM communication (rPtr > 0)
- FBProvider + Framebuffer appear in IOReg
- Legacy display engine registers are programmable via MMIO
