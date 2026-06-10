# GA104 GSP Driver — Bare Metal Instructions

## Requirements
- macOS Ventura (ou superior)
- GA104 GPU (RTX 3070 Ti)
- Lilu.kext (para plugin)

## Compilar
```bash
cd kext/Src
make compile-test
# MD5 deve ser: 59d28fb9172fcb4ac8e7a90ec2672719
```

## Instalar
```bash
cd kext/Src
make install  # copia para /tmp/GA104Driver.kext
sudo kmutil load -v --bundle-path /tmp/GA104Driver.kext
```

## Testar
```bash
sudo ~/kext/gsp_loader ~/kext/Resources/gsp_firmware.bin --bootloader ~/kext/tools/booter_load.bin
```

## Verificar
```bash
ioreg -l -w0 -r -c GA104Device | grep -E "rPtr|GSP_INIT"
```

Se `FinalrPtr > 0` — GSP-RM está a funcionar! 🚀

## Estrutura do código
- `bootGSP()` — tenta SEC2, fallback para GSP Falcon booter
- `gspSetupQueues()` — configura queues (cmdq + msgq)
- `sendGspRpcAllocRoot()` — envia RPCs de alloc via MSGQ

## Notas
- Em VFIO/QEMU, os registos RISC-V (0x111000+) estão locked
- Em bare metal, o SEC2 funciona e o doorbell chega à firmware
