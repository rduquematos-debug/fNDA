#!/bin/bash
# tail -f do serial log
# USO: ./vm-serial.sh

VM_DIR="${VM_DIR:-/path/to/vm/macos-ventura}"
SERIAL="$VM_DIR/serial.log"
[ -f "$SERIAL" ] || { echo "❌ Serial log não encontrado"; exit 1; }

echo "📋 A monitorizar: $SERIAL"
echo "🔍 Ctrl+C para sair"
echo ""
sudo tail -f "$SERIAL"
