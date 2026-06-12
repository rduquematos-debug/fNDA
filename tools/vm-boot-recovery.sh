#!/bin/bash
# Boot só RecoveryImage (sem disco do sistema)
# USO: ./vm-boot-recovery.sh

./vm-kill.sh 2>/dev/null

VM_DIR="/mnt/sda1/vm/macos-ventura"
cd /mnt/sda1/vm
sudo rm -f $VM_DIR/monitor.socket
sudo bash -c 'echo "" > $VM_DIR/serial.log && chmod 666 $VM_DIR/serial.log'

nohup /usr/bin/qemu-system-x86_64 \
    -name recovery \
    -machine q35,hpet=off,smm=off,vmport=off,accel=kvm \
    -global kvm-pit.lost_tick_policy=discard \
    -global ICH9-LPC.disable_s3=1 \
    -global ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off \
    -device isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal\(c\)AppleComputerInc \
    -global nec-usb-xhci.msi=off \
    -cpu Haswell-v2,vendor=GenuineIntel,-pdpe1gb,+avx,+sse,+sse2,+ssse3,vmware-cpuid-freq=on,+avx2,+sse4.2 \
    -smp cores=2,threads=2,sockets=1 \
    -m 16G \
    -display vnc=:0 -vga vmware \
    -device virtio-rng-pci \
    -object rng-random,id=rng0,filename=/dev/urandom \
    -device qemu-xhci -device usb-kbd -device usb-tablet -k en-us \
    -device virtio-net-pci,netdev=nic \
    -netdev user,id=nic,hostfwd=tcp::22220-:22 \
    -drive if=pflash,format=raw,unit=0,file=$VM_DIR/OVMF_CODE.fd,readonly=on \
    -drive if=pflash,format=raw,unit=1,file=$VM_DIR/OVMF_VARS-1920x1080.fd \
    -device ahci,id=ahci \
    -device ide-hd,bus=ahci.0,drive=BootLoader,bootindex=0 \
    -drive id=BootLoader,if=none,format=qcow2,file=$VM_DIR/OpenCore.qcow2 \
    -device ide-hd,bus=ahci.1,drive=RecoveryImage \
    -drive id=RecoveryImage,if=none,format=raw,file=$VM_DIR/RecoveryImage.img \
    -fsdev local,id=fsdev0,path=/home/rafaelm/Público,security_model=mapped-xattr \
    -device virtio-9p-pci,fsdev=fsdev0,mount_tag=Public-rafaelm \
    -monitor unix:$VM_DIR/monitor.socket,server,nowait \
    -serial file:$VM_DIR/serial.log > /dev/null 2>&1 &

echo "✅ Recovery a bootar (sem disco do sistema)"
echo "   VNC: localhost:5900"
