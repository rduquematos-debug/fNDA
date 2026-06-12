#!/bin/bash
# Shell interactiva no monitor QEMU
# USO: ./vm-monitor.sh [comando]

VM_DIR="${VM_DIR:-/path/to/vm/macos-ventura}"
MONITOR="$VM_DIR/macos-ventura-monitor.socket"
[ -S "$MONITOR" ] || MONITOR="$VM_DIR/monitor.socket"
[ -S "$MONITOR" ] || { echo "❌ VM não está a correr"; exit 1; }

if [ -n "$1" ]; then
    # Comando único
    echo "$*" | sudo socat - UNIX-CONNECT:$MONITOR 2>&1 | grep -v "qemu)" | grep -v "^$"
else
    # Shell interactiva
    echo "Monitor QEMU. Comandos comuns:"
    echo "  info status          - estado da VM"
    echo "  info usernet         - port forwarding"
    echo "  screendump /tmp/x.ppm - capturar ecrã"
    echo "  sendkey ret          - enviar Enter"
    echo "  sendkey down         - seta para baixo"
    echo "  quit                 - sair"
    echo ""
    sudo socat - UNIX-CONNECT:$MONITOR
fi
