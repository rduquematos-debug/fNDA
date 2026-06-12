#!/bin/bash
# Aplicar SIP off (csr-active-config = FF070000)
# USO: ./vm-sip-off.sh

if ! mount | grep -q "/mnt.*nbd"; then
    echo "⚠️  OpenCore não montado. A montar..."
    ./vm-mount-opencore.sh mount || exit 1
fi

sudo python3 -c "
import plistlib
cfg = plistlib.load(open('/mnt/EFI/OC/config.plist','rb'))
nv = cfg['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
nv['csr-active-config'] = bytes([0xFF, 0x07, 0x00, 0x00])
plistlib.dump(cfg, open('/mnt/EFI/OC/config.plist','wb'))
with open('/mnt/EFI/OC/config.plist','rb') as f:
    v = plistlib.load(f)
nv2 = v['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']
print(f'✅ SIP: {nv2[\"csr-active-config\"].hex()} ({nv2[\"csr-active-config\"].hex() == \"ff070000\"})')
"
