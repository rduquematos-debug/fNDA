#!/bin/bash
# Montar/desmontar OpenCore.qcow2 via NBD
# USO: ./vm-mount-opencore.sh mount|umount

NBD="/dev/nbd0"
EFI_MNT="/mnt"
OPEN_CORE="/mnt/sda1/vm/macos-ventura/OpenCore.qcow2"

case "$1" in
    mount)
        # Verificar se já montado
        mount | grep -q "$EFI_MNT.*nbd" && { echo "❌ Já montado em $EFI_MNT"; exit 1; }
        # Matar VMs primeiro
        ./vm-kill.sh 2>/dev/null
        # Ligar NBD
        sudo qemu-nbd --connect=$NBD "$OPEN_CORE" 2>/dev/null || {
            echo "❌ OpenCore.qcow2 não encontrado ou ocupado"
            exit 1
        }
        sleep 2
        sudo mount ${NBD}p1 $EFI_MNT 2>/dev/null || {
            echo "❌ Falha ao montar partição EFI"
            sudo qemu-nbd -d $NBD 2>/dev/null
            exit 1
        }
        echo "✅ OpenCore montado em $EFI_MNT"
        ls "$EFI_MNT/EFI/OC/" 2>/dev/null | head -5
        ;;
    umount)
        sudo umount $EFI_MNT 2>/dev/null
        sudo qemu-nbd -d $NBD 2>/dev/null
        echo "✅ OpenCore desmontado"
        ;;
    *)
        echo "USO: $0 mount|umount"
        exit 1
        ;;
esac
