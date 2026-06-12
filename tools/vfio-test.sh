#!/bin/bash
# vfio-test.sh — Single-GPU passthrough for GA104Driver testing
#
# WARNING: This script UNBINDS the NVIDIA GPU from the host driver
# and passes it to a QEMU VM. The host WILL LOSE DISPLAY OUTPUT
# during the test. Run this via SSH from another machine.
#
# Usage:
#   ./vfio-test.sh            # Full flow: unbind → VM → restore
#   ./vfio-test.sh unbind     # Only unbind GPU from nvidia → vfio-pci
#   ./vfio-test.sh vm         # Only run VM (assumes GPU already bound)
#   ./vfio-test.sh restore    # Rebind GPU to nvidia + restart SDDM
#   ./vfio-test.sh status     # Show current GPU binding state
#
# Configuration
VM_DIR="/mnt/sda1/vm"
MACOS_DIR="$VM_DIR/macos-ventura"
SERIAL_LOG="$MACOS_DIR/serial.log"
MONITOR_SOCKET="$MACOS_DIR/macos-ventura-monitor.socket"
GPU_VGA="0000:07:00.0"
GPU_AUDIO="0000:07:00.1"
GPU_VGA_ID="10de 2482"
GPU_AUDIO_ID="10de 228b"
SLEEP_AFTER_UNBIND=3

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

die()   { echo -e "${RED}[ERR]${NC} $1"; exit 1; }
info()  { echo -e "${GREEN}[OK]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
header(){ echo -e "\n${YELLOW}=== $1 ===${NC}"; }

check_gpu_state() {
    local driver
    driver=$(readlink "/sys/bus/pci/devices/$GPU_VGA/driver" 2>/dev/null | xargs basename 2>/dev/null)
    echo "$driver"
}

unbind_gpu() {
    header "UNBIND GPU FROM NVIDIA"
    local current
    current=$(check_gpu_state)
    if [ "$current" = "vfio-pci" ]; then
        info "GPU already bound to vfio-pci"
        return 0
    fi
    if [ "$current" != "nvidia" ]; then
        warn "GPU driver: $current (expected nvidia)"
    fi

    # Stop display manager (host loses display)
    echo "Stopping SDDM..."
    sudo systemctl stop sddm 2>/dev/null || warn "Failed to stop SDDM"
    sleep 2

    # Kill any remaining Xorg/Wayland sessions
    sudo killall -9 Xorg 2>/dev/null || true
    sleep 1

    # Unload NVIDIA modules in reverse dependency order
    echo "Unloading NVIDIA modules..."
    sudo rmmod nvidia_drm 2>/dev/null || warn "nvidia_drm not loaded"
    sudo rmmod nvidia_modeset 2>/dev/null || warn "nvidia_modeset not loaded"
    sudo rmmod nvidia_uvm 2>/dev/null || warn "nvidia_uvm not loaded"
    sudo rmmod nvidia 2>/dev/null || die "Failed to unload nvidia (GPU in use?)"
    info "NVIDIA modules unloaded"

    # Load vfio-pci if not already loaded
    if ! lsmod | grep -q vfio_pci; then
        sudo modprobe vfio-pci || die "Failed to load vfio-pci"
        info "vfio-pci loaded"
    fi

    # Bind GPU to vfio-pci via driver override
    echo "$GPU_VGA_ID" | sudo tee /sys/bus/pci/drivers/vfio-pci/new_id > /dev/null 2>&1 || true
    echo "$GPU_AUDIO_ID" | sudo tee /sys/bus/pci/drivers/vfio-pci/new_id > /dev/null 2>&1 || true
    sleep 1

    # Verify
    local new_driver
    new_driver=$(check_gpu_state)
    if [ "$new_driver" = "vfio-pci" ]; then
        info "GPU now bound to vfio-pci"
    else
        warn "GPU driver after unbind: $new_driver"
        # Try direct bind
        echo "$GPU_VGA" | sudo tee /sys/bus/pci/drivers/vfio-pci/bind > /dev/null 2>&1 || true
        echo "$GPU_AUDIO" | sudo tee /sys/bus/pci/drivers/vfio-pci/bind > /dev/null 2>&1 || true
        sleep 1
        new_driver=$(check_gpu_state)
        [ "$new_driver" = "vfio-pci" ] && info "GPU bound via direct bind" || warn "GPU driver: $new_driver"
    fi
}

restore_gpu() {
    header "RESTORE GPU TO NVIDIA"
    local current
    current=$(check_gpu_state)

    # Unbind from vfio-pci
    if [ "$current" = "vfio-pci" ]; then
        echo "$GPU_VGA" | sudo tee /sys/bus/pci/drivers/vfio-pci/unbind > /dev/null 2>&1 || true
        echo "$GPU_AUDIO" | sudo tee /sys/bus/pci/drivers/vfio-pci/unbind > /dev/null 2>&1 || true
        sleep 1
        # Remove IDs to prevent re-binding
        echo "$GPU_VGA_ID" | sudo tee /sys/bus/pci/drivers/vfio-pci/remove_id > /dev/null 2>&1 || true
        echo "$GPU_AUDIO_ID" | sudo tee /sys/bus/pci/drivers/vfio-pci/remove_id > /dev/null 2>&1 || true
        info "Unbound from vfio-pci"
    fi

    # Rebind to nvidia
    echo "$GPU_VGA" | sudo tee /sys/bus/pci/drivers/nvidia/bind > /dev/null 2>&1 || true
    echo "$GPU_AUDIO" | sudo tee /sys/bus/pci/drivers/nvidia/bind > /dev/null 2>&1 || true
    sleep 1

    # Reload NVIDIA modules
    sudo modprobe nvidia 2>/dev/null || warn "Failed to load nvidia"
    sudo modprobe nvidia_modeset 2>/dev/null || warn "Failed to load nvidia_modeset"
    sudo modprobe nvidia_uvm 2>/dev/null || true
    sudo modprobe nvidia_drm 2>/dev/null || true

    # Restart display manager
    sudo systemctl start sddm 2>/dev/null || warn "Failed to start SDDM"
    info "NVIDIA driver restored. Display should come back."
}

run_vm() {
    header "BOOT VM WITH VFIO"

    # Kill existing VM
    sudo killall -9 qemu-system-x86_64 2>/dev/null || true
    sleep 1
    sudo rm -f "$MONITOR_SOCKET"
    echo "" | sudo tee "$SERIAL_LOG" > /dev/null
    sudo chmod 666 "$SERIAL_LOG"

    # Verify GPU is bound to vfio-pci
    local driver
    driver=$(check_gpu_state)
    if [ "$driver" != "vfio-pci" ]; then
        die "GPU not bound to vfio-pci (current: $driver). Run '$0 unbind' first."
    fi

    cd "$VM_DIR"

    echo "Starting QEMU with VFIO..."
    echo "  GPU: $GPU_VGA (VGA) + $GPU_AUDIO (Audio)"
    echo "  VM:  $MACOS_DIR"
    echo "  Log: $SERIAL_LOG"
    echo ""

    sudo /usr/bin/qemu-system-x86_64 \
        -name macos-ventura,process=macos-ventura,debug-threads=on \
        -machine q35,hpet=off,smm=off,vmport=off,accel=kvm \
        -global kvm-pit.lost_tick_policy=discard \
        -global ICH9-LPC.disable_s3=1 \
        -global ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off \
        -device isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal\(c\)AppleComputerInc \
        -global nec-usb-xhci.msi=off \
        -cpu host,vendor=GenuineIntel,+sse,+sse2,+ssse3,+sse4.2,+avx,+avx2,vmware-cpuid-freq=on \
        -smp cores=4,threads=2,sockets=1 \
        -m 16G \
        -rtc base=utc,clock=host \
        -vga none \
        -device vmware-svga,vgamem_mb=256 \
        -device virtio-rng-pci,rng=rng0 \
        -object rng-random,id=rng0,filename=/dev/urandom \
        -device qemu-xhci,id=input \
        -device usb-kbd,bus=input.0 \
        -k en-us \
        -device usb-tablet,bus=input.0 \
        -device virtio-net-pci,netdev=nic \
        -netdev user,hostname=macos-ventura,hostfwd=tcp::22220-:22,id=nic \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,file=$MACOS_DIR/OVMF_CODE.fd,readonly=on \
        -drive if=pflash,format=raw,unit=1,file=$MACOS_DIR/OVMF_VARS-1920x1080.fd \
        -device ahci,id=ahci \
        -device ide-hd,bus=ahci.0,drive=BootLoader,bootindex=0 \
        -drive id=BootLoader,if=none,format=qcow2,file=$MACOS_DIR/OpenCore.qcow2 \
        -device ide-hd,bus=ahci.1,drive=RecoveryImage \
        -drive id=RecoveryImage,if=none,format=raw,file=$MACOS_DIR/RecoveryImage.img \
        -device virtio-blk-pci,drive=SystemDisk \
        -drive id=SystemDisk,if=none,format=qcow2,file=$MACOS_DIR/disk.qcow2 \
        -fsdev local,id=fsdev0,path=/home/rafaelm/Público,security_model=mapped-xattr \
        -device virtio-9p-pci,fsdev=fsdev0,mount_tag=Public-rafaelm \
        -device vfio-pci,host=07:00.0 \
        -device vfio-pci,host=07:00.1 \
        -monitor unix:"$MONITOR_SOCKET",server,nowait \
        -display none \
        -vnc :0 \
        -serial file:"$SERIAL_LOG" \
        "$@"

    local ret=$?
    info "QEMU exited with code $ret"
    return $ret
}

show_status() {
    header "GPU STATUS"
    local driver
    driver=$(check_gpu_state)
    echo "  GPU VGA  (07:00.0): $driver"
    echo "  GPU Audio(07:00.1): $(readlink /sys/bus/pci/devices/$GPU_AUDIO/driver 2>/dev/null | xargs basename 2>/dev/null || echo 'none')"
    echo ""
    echo "  NVIDIA modules:"
    lsmod | grep nvidia | awk '{printf "    %-20s %s\n", $1, $2}'
    echo ""
    echo "  VFIO devices:"
    ls -la /dev/vfio/ 2>/dev/null | grep -v total
    echo ""
    if [ -f /proc/20870/cmdline ] 2>/dev/null; then
        echo "  QEMU VM running (PID 20870, software mode)"
    fi
    if [ -f "$MONITOR_SOCKET" ]; then
        echo "  VFIO VM monitor socket exists"
    fi
}

# === Main ===
case "${1:-help}" in
    unbind)
        unbind_gpu
        ;;
    restore)
        restore_gpu
        ;;
    vm)
        shift
        run_vm "$@"
        ;;
    status)
        show_status
        ;;
    full)
        unbind_gpu
        echo ""
        echo "Waiting ${SLEEP_AFTER_UNBIND}s before starting VM..."
        sleep "$SLEEP_AFTER_UNBIND"
        run_vm
        local vm_exit=$?
        echo ""
        if [ $vm_exit -eq 0 ]; then
            info "VM exited cleanly. Restoring GPU..."
        else
            warn "VM exited with code $vm_exit. Restoring GPU..."
        fi
        restore_gpu
        ;;
    help|*)
        echo "Usage: $0 {unbind|restore|vm|full|status}"
        echo ""
        echo "  unbind    — Stop SDDM, unload nvidia, bind GPU to vfio-pci"
        echo "  vm        — Run QEMU VM with VFIO (GPU must be bound first)"
        echo "  restore   — Unbind from vfio, reload nvidia, restart SDDM"
        echo "  full      — unbind + vm + restore (automatic)"
        echo "  status    — Show current GPU binding and driver state"
        echo ""
        echo "Steps to test kext:"
        echo "  1. $0 unbind              # Unbind GPU (host loses display)"
        echo "  2. $0 vm                  # Boot macOS with VFIO"
        echo "  3. SSH to VM, load kext   # Test GA104Driver"
        echo "  4. Kill VM (Ctrl-C)       # VM exits"
        echo "  5. $0 restore             # Rebind GPU (display comes back)"
        exit 1
        ;;
esac
