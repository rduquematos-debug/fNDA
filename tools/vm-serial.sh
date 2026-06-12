#!/bin/bash
# tail -f do serial log
# USO: ./vm-serial.sh

SERIAL="/mnt/sda1/vm/macos-ventura/serial.log"
[ -f "$SERIAL" ] || { echo "❌ Serial log não encontrado"; exit 1; }

echo "📋 A monitorizar: $SERIAL"
echo "🔍 Ctrl+C para sair"
echo ""
sudo tail -f "$SERIAL"
