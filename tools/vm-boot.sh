#!/bin/bash
# Boot normal (Quickemu, VNC)
# USO: ./vm-boot.sh

./vm-kill.sh 2>/dev/null
VM_DIR="${VM_DIR:-/path/to/vm/macos-ventura}"
cd "$(dirname "$VM_DIR")"
sudo rm -f macos-ventura/monitor.socket macos-ventura/macos-ventura-monitor.socket
sudo bash -c 'echo "" > macos-ventura/serial.log && chmod 666 macos-ventura/serial.log'

nohup ./boot-vnc.sh > /dev/null 2>&1 &
echo "✅ VM a bootar (Quickemu)"
echo "   VNC: localhost:5900"
echo "   SSH: ${VM_SSH:-user@localhost -p 22220}"
