#!/bin/bash
# Boot VFIO com GPU real (RTX 3070 Ti)
# USO: ./vm-boot-vfio.sh

VM_DIR="${VM_DIR:-/path/to/vm/macos-ventura}"

./vm-kill.sh 2>/dev/null

cd "$(dirname "$VM_DIR")"
sudo rm -f $VM_DIR/monitor.socket
sudo bash -c 'echo "" > $VM_DIR/serial.log && chmod 666 $VM_DIR/serial.log'

sudo nohup /usr/bin/qemu-system-x86_64 \
    -name macos-ventura,process=macos-ventura \
    -machine q35,hpet=off,smm=off,vmport=off,accel=kvm \
    -global kvm-pit.lost_tick_policy=discard \
    -global ICH9-LPC.disable_s3=1 \
    -global ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off \
    -device isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal\(c\)AppleComputerInc \
    -global nec-usb-xhci.msi=off \
    -cpu Haswell-v2,vendor=GenuineIntel,-pdpe1gb,+avx,+sse,+sse2,+ssse3,vmware-cpuid-freq=on,+avx2,+sse4.2,+abm,+adx,+aes,+apic,+arat,+bmi1,+bmi2,+clflush,+cmov,+cx8,+cx16,+de,+erms,+f16c,+fma,+fsgsbase,+fxsr,+invpcid,+invtsc,+lahf-lm,+lm,+mca,+mce,+mmx,+movbe,+msr,+mtrr,+nx,+pae,+pat,-pcid,+pge,+pse,+popcnt,+pse36,+rdrand,+rdtscp,+sep,+smep,+syscall,+tsc,+vaes,+vpclmulqdq,+x2apic,+xgetbv1,+xsave,+xsaveopt,+tsc-deadline \
    -smp cores=2,threads=2,sockets=1 \
    -m 16G \
    -device virtio-balloon \
    -rtc base=utc,clock=host \
    -display vnc=:0 -vga none \
    -device vmware-svga,vgamem_mb=256 \
    -device vfio-pci,host=07:00.0 \
    -device vfio-pci,host=07:00.1 \
    -device virtio-rng-pci,rng=rng0 \
    -object rng-random,id=rng0,filename=/dev/urandom \
    -device nec-usb-xhci,id=spicepass \
    -chardev spicevmc,id=usbredirchardev1,name=usbredir \
    -device usb-redir,chardev=usbredirchardev1,id=usbredirdev1 \
    -device qemu-xhci,id=input \
    -device usb-kbd,bus=input.0 \
    -k en-us \
    -device usb-tablet,bus=input.0 \
    -audiodev spice,id=audio0 \
    -device virtio-sound-pci,audiodev=audio0 \
    -device virtio-net-pci,netdev=nic \
    -netdev user,hostname=macos-ventura,hostfwd=tcp::22220-:22,smb="${SHARE_DIR:-/path/to/share}",id=nic \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive if=pflash,format=raw,unit=0,file=$VM_DIR/OVMF_CODE.4m.fd,readonly=on \
    -drive if=pflash,format=raw,unit=1,file=$VM_DIR/OVMF_VARS.4m.fd \
    -device ahci,id=ahci \
    -device ide-hd,bus=ahci.0,drive=BootLoader,bootindex=0 \
    -drive id=BootLoader,if=none,format=qcow2,file=$VM_DIR/OpenCore.qcow2 \
    -device ide-hd,bus=ahci.1,drive=RecoveryImage \
    -drive id=RecoveryImage,if=none,format=raw,file=$VM_DIR/RecoveryImage.img \
    -device virtio-blk-pci,drive=SystemDisk \
    -drive id=SystemDisk,if=none,format=qcow2,file=$VM_DIR/disk.qcow2 \
    -fsdev local,id=fsdev0,path="${SHARE_DIR:-/path/to/share}",security_model=mapped-xattr \
    -device virtio-9p-pci,fsdev=fsdev0,mount_tag=Public-share \
    -monitor unix:$VM_DIR/macos-ventura-monitor.socket,server,nowait \
    -serial file:$VM_DIR/serial.log < /dev/null > /dev/null 2>&1 &

echo "✅ VM VFIO a bootar"
echo "   GPU RTX 3070 Ti passada"
echo "   VNC: localhost:5900"
echo "   Serial: tail -f $VM_DIR/serial.log"
