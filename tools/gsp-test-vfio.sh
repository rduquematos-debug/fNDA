#!/bin/bash
# gsp-test-vfio.sh — Boot VFIO VM and test GA104Driver kext
#
# Requirements:
#   - macOS VM with VFIO GPU passthrough (RTX 3070 Ti or GA104)
#   - Compiled GA104Driver.kext in project directory
#   - SSH key authentication to VM (or edit VM_SSH below)
#
# Usage:
#   ./gsp-test-vfio.sh               # Full test: boot + load + gsp_loader
#   ./gsp-test-vfio.sh boot          # Boot VM only
#   ./gsp-test-vfio.sh load          # Load kext and run gsp_loader
#   ./gsp-test-vfio.sh logs          # Show kernel logs
#
# Configuration (override via environment):
#   VM_SSH=user@host:port     (default: rafa@localhost:22220)
#   VM_DIR=/path/to/vm        (default: /mnt/sda1/vm/macos-ventura)
#   PROJECT_DIR=/path/to/fNDA (default: ../ relative to this script)
#   KEXT_NAME=GA104Driver.kext
#   GSP_FW=gsp_firmware.bin
#
# Example:
#   VM_DIR=$HOME/vm/macos-ventura ./gsp-test-vfio.sh

set -e

# Paths
PROJECT_DIR="${PROJECT_DIR:-$(cd "$(dirname "$0")/.." && pwd)}"
KEXT_NAME="${KEXT_NAME:-GA104Driver.kext}"
GSP_FW="${GSP_FW:-gsp_firmware.bin}"
VM_DIR="${VM_DIR:-/mnt/sda1/vm/macos-ventura}"
VM_SSH_USER="${VM_SSH_USER:-rafa}"
VM_SSH_PORT="${VM_SSH_PORT:-22220}"
VM_SSH_HOST="${VM_SSH_HOST:-localhost}"
VM_SSH_FULL="${VM_SSH:-$VM_SSH_USER@$VM_SSH_HOST -p $VM_SSH_PORT}"
SSH_CMD="ssh -o StrictHostKeyChecking=no -o PasswordAuthentication=no $VM_SSH_FULL"

die() { echo "[ERR] $1"; exit 1; }
info() { echo "[OK] $1"; }

# Show config
echo "=== GSP VFIO Test ==="
echo " Project:   $PROJECT_DIR"
echo " Kext:      $KEXT_NAME"
echo " VM SSH:    $VM_SSH_FULL"
echo " VM Dir:    $VM_DIR"
echo " Firmware:  $GSP_FW"
echo ""

case "${1:-all}" in
    boot)
        echo "=== Boot VFIO VM ==="
        # Kill any existing VM process
        killall -9 qemu-system-x86_64 2>/dev/null || true
        sleep 1

        # Boot VM with QEMU VFIO (customize for your GPU)
        # This is a template — adapt GPU PCI addresses and paths
        QEMU_CMD="qemu-system-x86_64 ..."
        # TODO: generate full QEMU command with VFIO GPU passthrough
        echo "Run your VM with VFIO enabled. Example:"
        echo "  \$QEMU_BIN -device vfio-pci,host=07:00.0 ..."
        echo ""
        echo "After boot, run: $0 load"
        ;;

    load)
        echo "=== Load Kext + gsp_loader ==="
        # Ensure kext exists
        if [ ! -f "$PROJECT_DIR/$KEXT_NAME/Contents/MacOS/GA104Driver" ]; then
            die "Kext not compiled. Run 'make' in Src/ first."
        fi

        # Copy kext to VM
        rsync -avz -e "ssh -p $VM_SSH_PORT" "$PROJECT_DIR/$KEXT_NAME" \
            "$VM_SSH_USER@localhost:/tmp/$KEXT_NAME" || die "rsync failed"

        # Load kext
        $SSH_CMD "sudo kmutil load -v --bundle-path /tmp/$KEXT_NAME" || \
            die "kmutil load failed (check SIP is disabled)"

        info "Kext loaded"

        # Check IOReg
        $SSH_CMD "ioreg -l -w0 -r -c GA104Device" | head -30
        echo ""

        # Run gsp_loader if firmware exists
        if [ -f "$PROJECT_DIR/Resources/$GSP_FW" ]; then
            info "Running gsp_loader with $GSP_FW..."
            rsync -avz -e "ssh -p $VM_SSH_PORT" "$PROJECT_DIR/tools/gsp_loader" \
                "$PROJECT_DIR/Resources/$GSP_FW" \
                "$VM_SSH_USER@localhost:~/"
            $SSH_CMD "cd ~ && ./gsp_loader $GSP_FW 2>&1" || \
                echo "[WARN] gsp_loader returned non-zero"
        else
            echo "[WARN] Firmware not found at Resources/$GSP_FW — skipping gsp_loader"
        fi

        # Print final state
        echo ""
        echo "=== Final IOReg ==="
        $SSH_CMD "ioreg -l -w0 -r -c GA104Device | grep -E 'rPtr|GSP_INIT|Final|ACTIVE'" || true
        ;;

    logs)
        echo "=== Kernel Logs (last 2 min) ==="
        $SSH_CMD "log show --predicate 'process == \"kernel\"' --last 2m | grep GA104"
        ;;

    all)
        $0 boot
        echo ""
        echo "=== Wait for VM boot, then run ==="
        $0 load
        $0 logs
        ;;

    *)
        echo "Usage: $0 {boot|load|logs|all}"
        exit 1
        ;;
esac
