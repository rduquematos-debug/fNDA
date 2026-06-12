#!/bin/bash
# Deploy script: Compile na VM + deploy para OpenCore + PEN
# USO: ./deploy.sh [vm|pen|all]

set -e

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="$PROJECT/tools"
SRC="$PROJECT/Src"
VM_SSH="${VM_SSH:-user@localhost -p 22220}"
VM_PASS="${VM_PASS:-}"

die() { echo "❌ $1"; exit 1; }
info() { echo "✅ $1"; }

case "${1:-all}" in
    vm|all)
        echo "=== 1. Copiar source para VM ==="
        scp -P 22220 $SRC/*.cpp $SRC/*.hpp $VM_SSH:~/fNDA/Src/ 2>/dev/null || die "SCP failed"
        scp -P 22220 $PROJECT/Info.plist $VM_SSH:~/fNDA/ 2>/dev/null
        info "Source copied"

        echo "=== 2. Compilar na VM ==="
        ssh $VM_SSH "cd ~/fNDA/Src && make clean 2>&1 && make all 2>&1" || die "Compile failed"
        info "Kext compiled"

        echo "=== 3. Copiar kext compilado para projeto ==="
        scp -P 22220 -r $VM_SSH:~/fNDA/GA104Driver.kext $PROJECT/ 2>/dev/null
        info "Kext copied to project ($(stat -c%s $PROJECT/GA104Driver.kext/Contents/MacOS/GA104Driver) bytes)"
        ;;
esac

case "${1:-all}" in
    pen|all)
        PEN_DEV="${PEN_DEV:-/dev/sdX}"
        PEN_MNT="${PEN_MNT:-/mnt/pen}"

        echo "=== 4. Deploy para PEN ==="
        if [ -b "$PEN_DEV" ]; then
            # Desmontar se montado
            mount | grep -q "$PEN_MNT" && sudo umount $PEN_MNT 2>/dev/null
            sudo mount ${PEN_DEV}1 $PEN_MNT 2>/dev/null || die "PEN not found at ${PEN_DEV}1"

            sudo rm -rf $PEN_MNT/EFI/OC/Kexts/GA104Driver.kext 2>/dev/null
            sudo cp -a $PROJECT/GA104Driver.kext $PEN_MNT/EFI/OC/Kexts/
            sync
            sudo umount $PEN_MNT
            info "PEN updated with new kext"
        else
            echo "⚠️  PEN not connected (${PEN_DEV}) - skipping"
        fi
        ;;&
    vm|all)
        echo "=== 5. Deploy para VM OpenCore ==="
        cd $TOOLS
        bash vm-kill.sh 2>/dev/null || true
        bash vm-deploy-kext.sh 2>/dev/null && info "VM OpenCore updated" || echo "⚠️  VM OpenCore deploy failed"
        ;;
esac

echo ""
echo "🎯 Done! Next:"
case "${1:-all}" in
    all)
        echo "   ./vm-boot-vfio.sh     # Boot VM VFIO"
        echo "   bash vm-monitor.sh \"sendkey ret\"  # Se preso no picker"
        echo "   ssh \$VM_SSH \"ioreg -l -w0 -r -c GA104Framebuffer\""
        ;;
    vm)
        echo "   ./vm-boot-vfio.sh"
        ;;
    pen)
        echo "   Boot bare metal com PEN"
        ;;
esac
