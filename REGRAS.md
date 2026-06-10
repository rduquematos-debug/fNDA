# fNDA Project Rules

## Communication

- Commits must be in **English**
- Code comments must be in **English**
- User can speak Portuguese or English — assistant adapts

## File Handling

- NEVER use `sed` on plist/XML files — always use `plistlib` (Python 3)
- NEVER save important files in `/tmp` — reboot destroys them
- Config files: always edit with `plistlib`, never with sed or manual XML manipulation

## Kext Management

- NEVER use `kmutil`, `kextutil`, or `mkutil` — corrupts kernel collection → irreversible bootloop
- NEVER place kexts in `/Library/Extensions/` — triggers syspolicyd approval + kmutil rebuild
- ALWAYS place kexts in `EFI/OC/Kexts/` — injected by OpenCore before macOS boots (no syspolicyd)
- Kernel collections are NEVER rebuilt — use OpenCore injection only

## Backup Strategy

- `disk.qcow2.clean`: Created ONCE after fresh macOS install. **READ-ONLY** (chmod 444). Never written to.
- `disk.qcow2.milestone`: Updated after each major milestone (working kext, VFIO, etc.)
- `disk.qcow2.wip`: Working copy. Copied from `.clean` or `.milestone`
- NEVER write to `.clean` — it's the inviolable recovery point

## Workflow

1. `cp disk.qcow2.clean disk.qcow2.wip` — start from clean base
2. Work on `.wip`
3. On milestone: `cp disk.qcow2.wip disk.qcow2.milestone`
4. If disaster: delete `.wip`, `cp disk.qcow2.milestone disk.qcow2.wip`
5. If total loss: `cp disk.qcow2.clean disk.qcow2.wip`

## Decision Making

- Before destructive operations (deleting files, modifying configs, installing packages) → confirm with user
- Before structural decisions (changing architecture, modifying boot config) → back-and-forth with user
- User gives "go" / "sim" → execute
- If uncertain → ask, don't assume

## VM Operations

- ALWAYS kill VM before mounting its disk images
- NBD connections must be properly disconnected after use
- Monitor socket (QEMU) is for debug and keyboard injection when necessary
