#!/bin/bash
# Copiar GA104Driver.kext compilado para EFI/OC/Kexts/
# USO: ./vm-deploy-kext.sh

KEXT_SRC="/home/rafaelm/Público/fNDA/GA104Driver.kext"
KEXT_DST="/mnt/EFI/OC/Kexts/GA104Driver.kext"

# Verificar se o kext existe e tem Info.plist
if [ ! -f "$KEXT_SRC/Contents/MacOS/GA104Driver" ]; then
    echo "❌ GA104Driver não compilado. Corre 'make compile-test' primeiro"
    exit 1
fi
[ -s "$KEXT_SRC/Contents/Info.plist" ] || { echo "❌ Info.plist vazio"; exit 1; }

# Montar OpenCore se necessário
if ! mount | grep -q "/mnt.*nbd"; then
    echo "⚠️  A montar OpenCore..."
    ./vm-mount-opencore.sh mount || exit 1
fi

# Copiar kext
sudo cp -r "$KEXT_SRC" "$KEXT_DST" && echo "✅ Kext copiado"

# Adicionar ao config.plist se não existir
sudo python3 -c "
import plistlib
cfg = plistlib.load(open('/mnt/EFI/OC/config.plist','rb'))
for k in cfg['Kernel']['Add']:
    if k['BundlePath'] == 'GA104Driver.kext':
        print('✅ Kext já existe no config')
        break
else:
    cfg['Kernel']['Add'].append({
        'Arch': 'x86_64', 'BundlePath': 'GA104Driver.kext',
        'Comment': 'NVIDIA GA104 Driver', 'Enabled': True,
        'ExecutablePath': 'Contents/MacOS/GA104Driver',
        'MaxKernel': '', 'MinKernel': '',
        'PlistPath': 'Contents/Info.plist',
    })
    plistlib.dump(cfg, open('/mnt/EFI/OC/config.plist','wb'))
    print('✅ Kext adicionado ao config')
"
