# Compiling fNDA

## Requirements

- macOS 13.0+ (Ventura)
- Xcode Command Line Tools (or full Xcode)
  ```bash
  xcode-select --install
  ```

## Build

```bash
cd Src
make clean
make all
```

## Output

The compiled kext is at `../GA104Driver.kext/`:

```
GA104Driver.kext/
├── Contents/
│   ├── Info.plist
│   ├── MacOS/
│   │   └── GA104Driver        # Mach-O 64-bit kext bundle
│   └── Resources/
```

## Deploy to EFI/OpenCore (bare metal)

```bash
# Mount your USB's EFI partition
sudo mount /dev/diskXs1 /mnt

# Copy kext
sudo cp -r GA104Driver.kext /mnt/EFI/OC/Kexts/

# Add to config.plist
# (Or use the included config.plist as base)
```

## Load on Bare Metal

```bash
sudo cp -r GA104Driver.kext /tmp/
sudo kmutil load -v --bundle-path /tmp/GA104Driver.kext
```

## Test

```bash
# Check if loaded
ioreg -l -w0 -r -c GA104Device

# Check framebuffer provider
ioreg -l -w0 -r -c GA104FBProvider

# Check framebuffer
ioreg -l -w0 -r -c GA104Framebuffer

# View kernel logs
log show --predicate 'process == "kernel"' --last 5m | grep GA104
```
