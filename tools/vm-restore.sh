#!/bin/bash
# Restaurar backup do disco
# USO: ./vm-restore.sh

VM_DIR="${VM_DIR:-/path/to/vm/macos-ventura}"
DISK="$VM_DIR/disk.qcow2"
CLEAN="$VM_DIR/disk.qcow2.clean"

[ -f "$CLEAN" ] || { echo "❌ Backup não encontrado: $CLEAN"; exit 1; }

# Verificar se VM está a correr
ps aux | grep -q "[q]emu.*disk.qcow2" && { echo "❌ VM está a correr. Mata primeiro."; exit 1; }

rm -f "$DISK"
cp "$CLEAN" "$DISK"
echo "✅ Disco restaurado de $CLEAN"
