#!/bin/bash
# Matar QEMU + NBDs, confirmar que morreram
killall -9 qemu-system-x86_64 2>/dev/null
sleep 2
# Verificar zombies
PID=$(ps aux | grep qemu | grep -v grep | awk '{print $2}')
if [ -n "$PID" ]; then
    sudo kill -9 $PID 2>/dev/null
    sleep 1
fi
# Limpar NBDs
for i in $(seq 0 15); do sudo qemu-nbd -d /dev/nbd$i 2>/dev/null; done
sleep 1
# Confirmar
ps aux | grep -E "qemu|nbd" | grep -v grep | grep -v kworker
echo "✅ VMs mortas e NBDs limpos"
