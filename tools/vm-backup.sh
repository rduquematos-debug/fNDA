#!/bin/bash
# Backup do disco da VM
# USO: ./vm-backup.sh

DISK="/mnt/sda1/vm/macos-ventura/disk.qcow2"
CLEAN="/mnt/sda1/vm/macos-ventura/disk.qcow2.clean"

[ -f "$DISK" ] || { echo "❌ disco não encontrado: $DISK"; exit 1; }

# Verificar se VM está a correr
ps aux | grep -q "[q]emu.*disk.qcow2" && { echo "❌ VM está a correr. Mata primeiro."; exit 1; }

cp "$DISK" "$CLEAN" && chmod 444 "$CLEAN"
echo "✅ Backup: $CLEAN ($(du -h $CLEAN | cut -f1))"
ls -la "$CLEAN"
