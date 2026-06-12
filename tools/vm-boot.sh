#!/bin/bash
# Boot normal (Quickemu, VNC)
# USO: ./vm-boot.sh

./vm-kill.sh 2>/dev/null
cd /mnt/sda1/vm
sudo rm -f macos-ventura/monitor.socket macos-ventura/macos-ventura-monitor.socket
sudo bash -c 'echo "" > macos-ventura/serial.log && chmod 666 macos-ventura/serial.log'

nohup ./boot-vnc.sh > /dev/null 2>&1 &
echo "✅ VM a bootar (Quickemu)"
echo "   VNC: localhost:5900"
echo "   SSH: rafa@localhost -p 22220"
