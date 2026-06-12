#include "GA104Device.hpp"
#include "GA104Regs.h"
#include "GA104DeviceUtilities.h"
#include "GSPFirmwareParser.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <kern/thread_call.h>
#include <IOKit/IOCommandGate.h>

// SEC2 embedded firmware
#include "SEC2Embed_Image.h"
#include "SEC2Embed_HsBlSig.h"
#include "SEC2Embed_Sig.h"

IOReturn GA104Device::setupRadix3()
{
    if (!fFWBuffer || fFWBufferSize == 0) return kIOReturnNotReady;
    return buildRadix3PageTable(fFWBufferSize);
}
IOReturn GA104Device::populateWprMeta()
{
    if (!fWprMetaBuf) {
        fWprMetaBuf = IOMallocAligned(GSP_FW_WPR_META_SIZE, 0x1000);
        if (!fWprMetaBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fWprMetaBuf, GSP_FW_WPR_META_SIZE, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fWprMetaPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fWprMetaPhys) { IOFreeAligned(fWprMetaBuf, GSP_FW_WPR_META_SIZE); fWprMetaBuf = nullptr; return kIOReturnNoMemory; }
    }

    GspFwWprMeta *meta = (GspFwWprMeta*)fWprMetaBuf;
    bzero(meta, sizeof(GspFwWprMeta));

    meta->magic = GSP_FW_WPR_META_MAGIC;
    meta->version = GSP_FW_WPR_META_REVISION;

    // Bootloader (GSP bootloader loaded via UserClient)
    GspBootInfo bl;
    if (parseGspBootloader((const uint8_t*)fBootloaderBuffer, fBootloaderSize, &bl)) {
        meta->bootloaderCodeOffset = (uint32_t)((uintptr_t)bl.imem_src - (uintptr_t)fBootloaderBuffer);
        meta->bootloaderCodeSize = bl.imem_size;
        meta->bootloaderDataOffset = (uint32_t)((uintptr_t)bl.dmem_src - (uintptr_t)fBootloaderBuffer);
        meta->bootloaderDataSize = bl.dmem_size;
        meta->bootloaderManifestOffset = bl.manifest_offset;
    }
    meta->sysmemAddrOfBootloader = fBootloaderPhys;
    meta->sizeOfBootloader = fBootloaderSize;

    // Radix3 page table + firmware
    meta->sysmemAddrOfRadix3Elf = fRadix3Phys;
    meta->sizeOfRadix3Elf = fFWBufferSize;

    meta->sysmemAddrOfSignature = fSec2SigPhys;
    meta->sizeOfSignature = gSec2SigSize;

    // VRAM/WPR2 layout (WPR2 disabled on GA104)
    meta->gspFwHeapVirtAddr = fVramLayout.heapAddr;
    meta->gspFwHeapSize = fVramLayout.heapSize;
    meta->gspFwOffset = fVramLayout.elfAddr;
    meta->bootBinVirtAddr = fVramLayout.bootAddr;
    meta->bootBinSize = 0x20000;
    meta->gspFwWprStart = 0;
    meta->gspFwWprEnd = 0;
    meta->fbSize = fVRAMSize;
    meta->nonWprHeapOffset = 0;
    meta->nonWprHeapSize = 0;
    meta->fwHeapEnabled = 1;
    meta->bootCount = 0;
    meta->verified = 0;

    __sync_synchronize();
    IOLog("GA104: WPR Meta at phys=0x%llx radix3=0x%llx fwPhys=0x%llx\n",
          fWprMetaPhys, fRadix3Phys, fFWBufferPhys);
    return kIOReturnSuccess;
}
IOReturn GA104Device::bootSEC2()
{
    if (!fBar0Virt) {
        IOLog("GA104: SEC2: no BAR0 mapping\n");
        return kIOReturnNotReady;
    }

    IOLog("GA104: SEC2 boot starting\n");
    setProperty("GA104SEC2_Start", true);

    // Allocate system memory for SEC2 firmware
    void *imageBuf = IOMallocAligned(gSec2FirmwareImageSize, 0x1000);
    void *hsBlBuf = IOMallocAligned(gSec2HsBlSigSize, 0x1000);
    void *sigBuf = IOMallocAligned(gSec2SigSize, 0x1000);
    if (!imageBuf || !hsBlBuf || !sigBuf) {
        if (imageBuf) IOFreeAligned(imageBuf, gSec2FirmwareImageSize);
        if (hsBlBuf) IOFreeAligned(hsBlBuf, gSec2HsBlSigSize);
        if (sigBuf) IOFreeAligned(sigBuf, gSec2SigSize);
        IOLog("GA104: SEC2: buffer alloc failed\n");
        return kIOReturnNoMemory;
    }

    memcpy(imageBuf, gSec2FirmwareImage, gSec2FirmwareImageSize);
    memcpy(hsBlBuf, gSec2HsBlSig, gSec2HsBlSigSize);
    memcpy(sigBuf, gSec2Sig, gSec2SigSize);

    // Get physical addresses
    IOMemoryDescriptor *md;
    uint64_t imagePhys = 0, hsBlPhys = 0, sigPhys = 0;

    md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)imageBuf, gSec2FirmwareImageSize, kIODirectionIn, kernel_task);
    if (md) { md->prepare(); imagePhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }

    md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)hsBlBuf, gSec2HsBlSigSize, kIODirectionIn, kernel_task);
    if (md) { md->prepare(); hsBlPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }

    md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)sigBuf, gSec2SigSize, kIODirectionIn, kernel_task);
    if (md) { md->prepare(); sigPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }

    if (!imagePhys || !hsBlPhys || !sigPhys) {
        IOFreeAligned(imageBuf, gSec2FirmwareImageSize);
        IOFreeAligned(hsBlBuf, gSec2HsBlSigSize);
        IOFreeAligned(sigBuf, gSec2SigSize);
        IOLog("GA104: SEC2: phys addr failed\n");
        return kIOReturnNoMemory;
    }

    IOLog("GA104: SEC2: imagePhys=0x%llx hsBlPhys=0x%llx sigPhys=0x%llx\n",
          imagePhys, hsBlPhys, sigPhys);

    // Save SEC2 signature physical address for populateWprMeta()
    if (!fSec2SigBuf) {
        fSec2SigBuf = sigBuf;
        fSec2SigPhys = sigPhys;
        sigBuf = nullptr; // ownership transferred
    }

    // Populate WPR Meta (Radix3 + firmware + bootloader + signature pointers)
    if (fWprMetaBuf) memset(fWprMetaBuf, 0, GSP_FW_WPR_META_SIZE);
    populateWprMeta();

    // Check current SEC2 state
    uint32_t sec2Cpuctl = readAbsReg32(NV_PSEC_FALCON_CPUCTL);
    IOLog("GA104: SEC2: initial CPUCTL=0x%08x\n", sec2Cpuctl);

    #define SEC2_REG(off, val) writeAbsReg32(NV_PSEC_BASE + (off), (val))
    #define SEC2_RD(off)  readAbsReg32(NV_PSEC_BASE + (off))

    // Reset SEC2 Falcon engine
    uint32_t sec2Eng = readAbsReg32(NV_PSEC_BASE + 0x03C0); // FALCON_ENGINE
    SEC2_REG(0x03C0, sec2Eng | 0x01);
    IODelay(10);
    SEC2_REG(0x03C0, sec2Eng & ~0x01);
    IODelay(10);

    // Wait for MEM_SCRUBBING — SEC2 is now in Falcon mode (reset state)
    for (int i = 0; i < 1000; i++) {
        if (!(SEC2_RD(0x00F4) & 0x10)) break; // HWCFG2 MEM_SCRUBBING
        IODelay(1);
    }

    // Read CPUCTL after reset (should now be readable since SEC2 is in Falcon mode)
    sec2Cpuctl = SEC2_RD(0x0100);
    IOLog("GA104: SEC2: after reset CPUCTL=0x%08x\n", sec2Cpuctl);

    // Enable FBIF
    SEC2_REG(0x0624, 0x180); // FALCON_FBIF_CTL

    // DMA image.bin to IMEM (use IMEM DMA: cmd 0x5)
    SEC2_REG(0x0110, (uint32_t)(imagePhys & 0xFFFFFFFF));  // DMATRFBASE lo
    SEC2_REG(0x0128, (uint32_t)((imagePhys >> 32) & 0xFFFFFFFF)); // DMATRFBASE1 hi
    SEC2_REG(0x0114, 0);  // DMATRFMOFFS = 0 (IMEM offset)
    SEC2_REG(0x0118, 0x5 | dma_size_encoding(gSec2FirmwareImageSize)); // DMATRFCMD
    __sync_synchronize();
    for (int i = 0; i < 1000; i++) {
        if (SEC2_RD(0x0118) & 0x2) break; // DMATRFCMD DONE
        IODelay(1);
    }
    IOLog("GA104: SEC2: IMEM DMA done (%u bytes)\n", gSec2FirmwareImageSize);

    // DMA hs_bl_sig.bin to DMEM offset 0 (DMEM DMA: cmd 0x1)
    SEC2_REG(0x0110, (uint32_t)(hsBlPhys & 0xFFFFFFFF));
    SEC2_REG(0x0128, (uint32_t)((hsBlPhys >> 32) & 0xFFFFFFFF));
    SEC2_REG(0x0114, 0);
    SEC2_REG(0x0118, 0x1 | dma_size_encoding(gSec2HsBlSigSize));
    __sync_synchronize();
    for (int i = 0; i < 1000; i++) {
        if (SEC2_RD(0x0118) & 0x2) break;
        IODelay(1);
    }
    IOLog("GA104: SEC2: DMEM HS BL done (%u bytes)\n", gSec2HsBlSigSize);

    // DMA sig.bin to DMEM offset 0x400 (PKC data offset from desc)
    SEC2_REG(0x0110, (uint32_t)(sigPhys & 0xFFFFFFFF));
    SEC2_REG(0x0128, (uint32_t)((sigPhys >> 32) & 0xFFFFFFFF));
    SEC2_REG(0x0114, 0x400);  // PKC data offset in DMEM
    SEC2_REG(0x0118, 0x1 | dma_size_encoding(gSec2SigSize));
    __sync_synchronize();
    for (int i = 0; i < 1000; i++) {
        if (SEC2_RD(0x0118) & 0x2) break;
        IODelay(1);
    }
    IOLog("GA104: SEC2: DMEM sig done (%u bytes at 0x400)\n", gSec2SigSize);

    // BROM signature validation (RSA-3K)
    SEC2_REG(0x1210, 0x400);     // BROM_PARAADDR = DMEM 0x400 (sig starts here)
    SEC2_REG(0x119C, 0x0000);    // BROM_ENGIDMASK
    SEC2_REG(0x1198, 0x00);      // BROM_CURR_UCODE_ID
    __sync_synchronize();
    SEC2_REG(0x0180, 0x01);      // BROM_MOD_SEL = RSA3K
    __sync_synchronize();

    // Poll BROM_MOD_SEL until clear (validation done)
    bool bromDone = false;
    for (int i = 0; i < 1000; i++) {
        if (!(SEC2_RD(0x0180) & 1)) { bromDone = true; break; }
        IODelay(10);
    }
    if (!bromDone) {
        IOLog("GA104: SEC2: BROM validation timeout\n");
        setProperty("GA104SEC2_BromTimeout", true);
    } else {
        IOLog("GA104: SEC2: BROM validation done\n");
    }

    // Set BOOTVEC and STARTCPU
    SEC2_REG(0x0104, 0);  // BOOTVEC = IMEM offset 0
    __sync_synchronize();

    // Write WPR Meta address to SEC2 MAILBOX0/1 (SEC2 bootloader reads this)
    SEC2_REG(0x0040, (uint32_t)(fWprMetaPhys & 0xFFFFFFFF));
    SEC2_REG(0x0044, (uint32_t)((fWprMetaPhys >> 32) & 0xFFFFFFFF));
    __sync_synchronize();
    IOLog("GA104: SEC2: WPR Meta addr=0x%llx written to MAILBOX0/1\n", fWprMetaPhys);

    // Write MCTP GSP-FMC packet to DMEM BEFORE STARTCPU (host has full DMEM access)
    if (fFWBufferPhys) {
        void *mctpBuf = IOMallocAligned(1024, 0x1000);
        if (mctpBuf) {
            uint8_t *pkt = (uint8_t*)mctpBuf;
            bzero(pkt, 1024);
            pkt[0] = 0x01; pkt[3] = 0xC0;  // MCTP: SOM=1, EOM=1, VER=1
            pkt[4] = 0x7E; pkt[5] = 0xDE; pkt[6] = 0x10; pkt[7] = 0x14;  // NVDM: TYPE=0x14
            *(uint16_t*)(pkt+8) = 1;        // COT ver
            *(uint16_t*)(pkt+10) = 912;     // COT size
            *(uint64_t*)(pkt+12) = fFWBufferPhys; // gspFmcSysmemOffset
            if (fHasCOTPayload) {
                memcpy(pkt + 36, fCOTPayload, 48);        // hash384 at COT+36
                memcpy(pkt + 84, fCOTPayload + 48, 384);   // publicKey at COT+84
                memcpy(pkt + 468, fCOTPayload + 432, 384); // signature at COT+468
                IOLog("GA104: SEC2: COT payload injected (hash=%02x%02x...)\n", fCOTPayload[0], fCOTPayload[1]);
            }
            if (fLibosPhys)
                *(uint64_t*)(pkt+908) = fLibosPhys; // gspBootArgsSysmemOffset

            IOMemoryDescriptor *mdM = IOMemoryDescriptor::withAddressRange(
                (mach_vm_address_t)mctpBuf, 1024, kIODirectionIn, kernel_task);
            uint64_t mctpPhys = 0;
            if (mdM) { mdM->prepare(); mctpPhys = mdM->getPhysicalSegment(0, nullptr); mdM->complete(); mdM->release(); }
            if (mctpPhys) {
                SEC2_REG(0x0110, (uint32_t)(mctpPhys & 0xFFFFFFFF));
                SEC2_REG(0x0128, (uint32_t)((mctpPhys >> 32) & 0xFFFFFFFF));
                SEC2_REG(0x0114, 0x800); // DMEM offset = EMEM channel (NVIDIA standard)
                SEC2_REG(0x0118, 0x1 | dma_size_encoding(1024));
                __sync_synchronize();
                for (int i = 0; i < 1000; i++) {
                    if (SEC2_RD(0x0118) & 0x2) break;
                    IODelay(1);
                }
                IOLog("GA104: SEC2: MCTP GSP-FMC written to DMEM+0x800 before STARTCPU fwPhys=0x%llx\n", fFWBufferPhys);
            }
            IOFreeAligned(mctpBuf, 1024);
        }
    }

    SEC2_REG(0x0100, 0x02);  // CPUCTL = STARTCPU
    __sync_synchronize();

    // Poll for HALTED (bit 4) or timeout, checking MAILBOX0 for status
    bool halted = false;
    uint32_t mb0_final = 0;
    uint32_t mb0_initial = SEC2_RD(0x0040);
    uint32_t mb1_val = SEC2_RD(0x0044);
    IOLog("GA104: SEC2: MAILBOX0 before=0x%08x MAILBOX1=0x%08x\n", mb0_initial, mb1_val);
    for (int i = 0; i < 500; i++) {
        uint32_t cpuctl = SEC2_RD(0x0100);
        uint32_t mb0 = SEC2_RD(0x0040);
        if (mb0 != mb0_initial && mb0 != 0) {
            IOLog("GA104: SEC2: MAILBOX0 changed! CPUCTL=0x%08x MB0=0x%08x (delta=%dms)\n",
                  cpuctl, mb0, i * 10);
            setProperty("GA104SEC2_Mb0Changed", mb0, 32);
        }
        if (cpuctl & 0x10) { // HALTED
            IOLog("GA104: SEC2: HALTED after %dms! CPUCTL=0x%08x MB0=0x%08x\n",
                  i * 10, cpuctl, mb0);
            setProperty("GA104SEC2_HaltedMs", (uint64_t)(i * 10), 64);
            setProperty("GA104SEC2_Cpuctl", cpuctl, 32);
            setProperty("GA104SEC2_Mailbox0", mb0, 32);
            halted = true;
            IOLog("GA104: SEC2 HALTED mb0=0x%08x mb1=0x%08x fSEC2Booted=%d\n",
                  mb0, SEC2_RD(0x0044), fSEC2Booted);
            break;
        }
        IOSleep(10);
    }

    if (!halted) {
        uint32_t finalCpuctl = SEC2_RD(0x0100);
        uint32_t mb0 = SEC2_RD(0x0040);
        IOLog("GA104: SEC2: running after 5s (CPUCTL=0x%08x MB0=0x%08x init_MB0=0x%08x BROM=%d)\n",
              finalCpuctl, mb0, mb0_initial, bromDone);
        setProperty("GA104SEC2_Running", true);
    }

    fSEC2Booted = halted && (mb0_final == 0);

    // Need to read mb0 BEFORE undef'ing the macros
    uint32_t sec2_mb0 = SEC2_RD(0x0040);

    // Free buffers
    IOFreeAligned(imageBuf, gSec2FirmwareImageSize);
    IOFreeAligned(hsBlBuf, gSec2HsBlSigSize);
    IOFreeAligned(sigBuf, gSec2SigSize);

    #undef SEC2_REG
    #undef SEC2_RD

    IOLog("GA104: SEC2 boot complete (halted=%d mb0=0x%08x, fSEC2Booted=%d)\n",
          halted, sec2_mb0, fSEC2Booted);
    setProperty("GA104SEC2_Done", halted);
    return halted ? kIOReturnSuccess : kIOReturnTimeout;
}
IOReturn GA104Device::setCOTPayload(const uint8_t *data, uint32_t size)
{
    if (!data || size == 0) {
        fHasCOTPayload = false;
        return kIOReturnSuccess;
    }
    uint32_t copySz = (size < 864) ? size : 864;
    memcpy(fCOTPayload, data, copySz);
    if (copySz < 864) memset(fCOTPayload + copySz, 0, 864 - copySz);
    fHasCOTPayload = true;
    IOLog("GA104: COT payload set (%u bytes, sig marker=0x%02x%02x...)\n",
          size, data[0], data[1]);
    setProperty("GA104COT_Set", true);
    setProperty("GA104COT_Size", (uint64_t)size, 32);
    return kIOReturnSuccess;
}
IOReturn GA104Device::readCSRs()
{
    if (!fGSPBase) return kIOReturnNotReady;

    // Step 1: Reset ICD state machine
    writeReg32(FALCON_ICD_CS, 0);
    __sync_synchronize();
    IODelay(1);

    // Step 2: Read CSRs
    static const uint32_t kCSRs[] = {0x5CA, 0x5CB, 0x5CC, 0x5CD, 0x5CE, 0x5CF, 0x5D0, 0x5D1, 0x8D0};
    uint32_t vals[9];

    for (int i = 0; i < 9; i++) {
        writeReg32(FALCON_ICD_RW, kCSRs[i]);
        __sync_synchronize();
        writeReg32(FALCON_ICD_CS, 0x01);  // START, no INDEX mode
        __sync_synchronize();

        for (int j = 0; j < 1000; j++) {
            uint32_t cs = readReg32(FALCON_ICD_CS);
            if (!(cs & 1)) break;  // BUSY clear = done
            IODelay(1);
        }

        vals[i] = readReg32(FALCON_ICD_RW);
        IOLog("GA104: CSR 0x%03X = 0x%08X\n", kCSRs[i], vals[i]);
        // Reset ICD between reads
        writeReg32(FALCON_ICD_CS, 0);
        __sync_synchronize();
        IODelay(1);
    }

    setProperty("GA104CSR_5CA", vals[0], 32);
    setProperty("GA104CSR_5CB", vals[1], 32);
    setProperty("GA104CSR_5CC", vals[2], 32);
    setProperty("GA104CSR_5CD", vals[3], 32);
    setProperty("GA104CSR_5CE", vals[4], 32);
    setProperty("GA104CSR_5CF", vals[5], 32);
    setProperty("GA104CSR_5D0", vals[6], 32);
    setProperty("GA104CSR_5D1", vals[7], 32);
    setProperty("GA104CSR_8D0", vals[8], 32);

    // Trampoline values for comparison
    IOLog("GA104: TRAMPOLINE CSR comparison:\n");
    IOLog("  5CA DATA_PTR:       ICD=0x%08X  TRAMP=0x00000008\n", vals[0]);
    IOLog("  5CB INST_CTRL:      ICD=0x%08X  TRAMP=0x10076001\n", vals[1]);
    IOLog("  5CC INST_REG0:      ICD=0x%08X  TRAMP=0x10076000\n", vals[2]);
    IOLog("  5CD INST_REG1:      ICD=0x%08X  TRAMP=(not set)\n", vals[3]);
    IOLog("  5CE DATA_REG0:      ICD=0x%08X  TRAMP=0x00000001\n", vals[4]);
    IOLog("  5CF DATA_REG1:      ICD=0x%08X  TRAMP=0x00040028\n", vals[5]);
    IOLog("  5D0 (extra):        ICD=0x%08X\n", vals[6]);
    IOLog("  5D1 (extra):        ICD=0x%08X\n", vals[7]);
    IOLog("  8D0 (extra):        ICD=0x%08X\n", vals[8]);

    return kIOReturnSuccess;
}
IOReturn GA104Device::bootGSP()
{
    setProperty("GA104P2_Start", true);
    setProperty("GA104P2_BootFlag", (uint64_t)fGSPBooted, 8);

    if (!fBar0Virt) {
        setProperty("GA104P2_NoBar0", true);
        return kIOReturnNotReady;
    }
    setProperty("GA104GSP_Step0_Base", true);

    // Read Falcon state via FALCON_CPUCTL (BAR0+0x110100)
    uint32_t cpuctl = readReg32(FALCON_CPUCTL);
    uint32_t mailbox0 = readReg32(FALCON_MAILBOX0);
    setProperty("GA104GSP_Step1_CPUCTL", cpuctl, 32);
    setProperty("GA104GSP_Step1_Mailbox0", mailbox0, 32);
    IOLog("GA104: GSP CPUCTL=0x%08x mb0=0x%x\n", cpuctl, mailbox0);
    IOReturn ret;

    // === Prepare GSP environment (used by both SEC2 and direct boot) ===
    ret = calculateVramLayout();
    if (ret != kIOReturnSuccess) return ret;

    if (!fGSPProtocol) {
        fGSPProtocol = new GSPProtocol;
        if (fGSPProtocol) fGSPProtocol->init();
    }

    ret = gspSetupQueues();
    if (ret != kIOReturnSuccess) return ret;

    // === Check for Direct Booter Boot first (skips SEC2) ===
    GspBootInfo booterInfo = {};
    IOLog("GA104: DEBUG hasBooter: buf=%p size=%u\n", fBootloaderBuffer, fBootloaderSize);
    bool hasBooter = (fBootloaderBuffer && fBootloaderSize > 0 &&
                      parseBooterLoadV3((const uint8_t*)fBootloaderBuffer,
                                        fBootloaderSize, &booterInfo));
    IOLog("GA104: DEBUG hasBooter=%d\n", hasBooter ? 1 : 0);

    if (hasBooter) {
        IOLog("GA104: hasBooter but trying SEC2 path first (BCR fix + fSEC2Booted fix)\n");
        setProperty("GA104_BooterPath", false);
    }

    // === SEC2 booter + GSP trampoline ===
    setupRadix3();
    if (!fWprMetaBuf) {
        fWprMetaBuf = IOMallocAligned(GSP_FW_WPR_META_SIZE, 0x1000);
        if (fWprMetaBuf) {
            IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
                (mach_vm_address_t)fWprMetaBuf, GSP_FW_WPR_META_SIZE, kIODirectionInOut, kernel_task);
            if (md) { md->prepare(); fWprMetaPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        }
    }
    ret = bootSEC2();
    if (ret == kIOReturnSuccess || fSEC2Booted) {
        IOLog("GA104: SEC2 booted with GSP-FMC embedded!\n");

        // Ler estado do GSP após SEC2 (antes de qualquer boot directo)
        uint32_t gspCpuctl = readReg32(FALCON_CPUCTL);
        uint32_t gspBootvec = readReg32(FALCON_BOOTVEC);
        uint32_t gspMb0 = readReg32(FALCON_MAILBOX0);
        uint32_t gspMb1 = readReg32(FALCON_MAILBOX1);
        IOLog("GA104: GSP after SEC2: CPUCTL=0x%08x BOOTVEC=0x%08x MB0=0x%08x MB1=0x%08x\n",
              gspCpuctl, gspBootvec, gspMb0, gspMb1);
        uint32_t riscvCpuctl = readAbsReg32(0x00111388);
        IOLog("GA104: SEC2_DONE — GSP RISC-V BCR=0x%08x IRQSTAT=0x%08x RISCV_CPUCTL=0x%08x\n",
              readReg32(FALCON_BCR_CTRL),
              readReg32(FALCON_IRQSTAT),
              riscvCpuctl);
        bool riscvActive = (riscvCpuctl & 0x80) != 0;
        IOLog("GA104: RISC-V %s\n", riscvActive ? "ACTIVE ✅" : "NOT ACTIVE ❌");
        setProperty("GA104_RISCV_ACTIVE", riscvActive);

        if (fVramCmdqEntryBase) {
            uint32_t *vramCmdqReadPtr = (uint32_t*)((fVramCmdqEntryBase - GSP_QUEUE_PAGE_SIZE)) + 0x20 / 4;
            *vramCmdqReadPtr = 0;
            __sync_synchronize();
            IOLog("GA104: SEC2: VRAM cmdq readPtr reset to 0\n");
        }

         if (!(gspCpuctl & 0x10) || riscvActive) {
             // CASO A: GSP already running — firmware loaded by SEC2 in WPR2.
             // Falcon may be HALTED but RISC-V can still be ACTIVE (firmware on RISC-V)
             IOLog("GA104: GSP running after SEC2! Proceeding to ALLOC_ROOT test.\n");
             fGSPBooted = true;
             writeReg32(FALCON_OS, 0x00010001);  // app version
             __sync_synchronize();
        } else {
            // CASO C: GSP halted and RISC-V not active after SEC2 — error
            IOLog("GA104: GSP halted and RISC-V inactive after SEC2, aborting boot\n");
            return kIOReturnError;
        }

        // Queue pre-boot RPCs: SET_SYSTEM_INFO + SET_REGISTRY (como no SEC2 path)
        if (fGSPProtocol && fCmdqTx && fBar1Phys) {
            IOLog("GA104: Pre-boot RPCs with host BARs...\n");
            uint32_t wp = fCmdqTx->writePtr;
            auto wrVRAM = [&](uint64_t off, const void *buf, uint32_t sz) {
                IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
                    fBar1Phys + off, sz, kIODirectionOut);
                if (md) { md->prepare(); md->writeBytes(0, buf, sz); md->complete(); md->release(); }
            };
            auto qEntry = [&](GspRpcMessageHeader *hdr, const void *pay, uint32_t paySz) {
                uint8_t elem[GSP_QUEUE_MSG_SIZE]; bzero(elem, sizeof(elem));
                GspMsgQueuePrefix *pre = (GspMsgQueuePrefix*)elem;
                pre->mctpHeader = GSP_MCTP_HEADER_SINGLE; pre->nvdmHeader = GSP_NVDM_HEADER_RM_RPC;
                memcpy(elem + sizeof(GspMsgQueuePrefix), hdr, sizeof(GspRpcMessageHeader));
                if (pay && paySz) memcpy(elem + sizeof(GspMsgQueuePrefix) + sizeof(GspRpcMessageHeader), pay, paySz);
                pre->seqNum = wp;
                uint64_t cs = 0;
                for (uint32_t ci = 0; ci < sizeof(elem) / 8; ci++) cs ^= ((uint64_t*)elem)[ci];
                pre->checksum = (uint32_t)((cs >> 32) ^ (cs & 0xFFFFFFFF));
                uint32_t qIdx = wp % GSP_QUEUE_MSG_COUNT;
                uint32_t copySz = sizeof(GspMsgQueuePrefix) + sizeof(GspRpcMessageHeader) + paySz;
                memcpy(fCmdqEntryBase + qIdx * GSP_QUEUE_MSG_SIZE, elem, copySz);
                // Also copy to VRAM via DMA (firmware reads from VRAM copy)
                if (fBar1Phys && fVramLayout.queuePhysAddr) {
                    uint64_t vramOff = fVramLayout.queuePhysAddr + fCmdqOff + GSP_QUEUE_PAGE_SIZE +
                        qIdx * GSP_QUEUE_MSG_SIZE;
                    wrVRAM(vramOff, elem, copySz);
                }
                wp++;
            };
            GspRpcMessageHeader siMsg;
            fGSPProtocol->buildSetSystemInfo(&siMsg, fBar0Phys, fBar1Phys, fBar2Phys, fDeviceID, 0, fRevision, fBDF);
            qEntry(&siMsg, (uint8_t*)&siMsg + sizeof(GspRpcMessageHeader),
                   siMsg.length - sizeof(GspRpcMessageHeader));
            uint32_t regBuf[256]; uint32_t regLen = sizeof(regBuf);
            GspRpcMessageHeader rgMsg;
            fGSPProtocol->buildSetRegistry(&rgMsg, regBuf, &regLen);
            qEntry(&rgMsg, regBuf, regLen);
            fCmdqTx->writePtr = wp;
            // Write writePtr to SHARED MEMORY (firmware reads from here, not from local copy)
            *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x10) = wp;
            // Also write to VRAM copy at the correct queue address
            if (fBar1Phys && fVramLayout.queuePhysAddr)
                wrVRAM(fVramLayout.queuePhysAddr + fCmdqOff + 0x10, &wp, 4);
            __sync_synchronize();
            IOLog("GA104: Pre-boot RPCs done (%u entries)\n", wp);

            // DEBUG: dump LibOS init args
            IOLog("GA104: LibOS dump (fLibosPhys=0x%llx):\n", fLibosPhys);
            for (int li = 0; li < 4; li++) {
                volatile uint64_t *lib64 = (volatile uint64_t*)((uint8_t*)fLibosBuf + li * 32);
                uint8_t *id = (uint8_t*)&lib64[0];
                IOLog("  [%d] id=0x%016llx ('%c%c%c%c%c%c%c%c') pa=0x%llx sz=%llu\n",
                      li, lib64[0],
                      id[0],id[1],id[2],id[3],id[4],id[5],id[6],id[7],
                      lib64[1], lib64[2]);
            }
            // DEBUG: dump RMARGS
            IOLog("GA104: RMARGS dump (fRmargsPhys=0x%llx):\n", fRmargsPhys);
            volatile uint32_t *rm32 = (volatile uint32_t*)fRmargsBuf;
            IOLog("  shmPA=0x%08x%08x pteCnt=%u cmdqOff=0x%x statqOff=0x%x\n",
                  rm32[1], rm32[0], rm32[2], rm32[4], rm32[6]);
            IOLog("  hdrSz=%u elMin=%u elMax=%u hdrAlign=%u elAlign=%u\n",
                  (uint32_t)rm32[8], (uint32_t)rm32[10], (uint32_t)rm32[12],
                  rm32[14], rm32[15]);
            // DEBUG: cmp cmdq entry 0 sysmem vs VRAM
            IOLog("GA104: CMDQ entry[0] sysmem:\n");
            volatile uint32_t *ce = (volatile uint32_t*)fCmdqEntryBase;
            for (int ci = 0; ci < 16; ci++)
                if (ce[ci]) IOLog("  [0x%02x] 0x%08x\n", ci*4, ce[ci]);
            if (fVramCmdqEntryBase) {
                volatile uint32_t *ve = (volatile uint32_t*)fVramCmdqEntryBase;
                IOLog("GA104: CMDQ entry[0] VRAM:\n");
                for (int ci = 0; ci < 16; ci++)
                    if (ve[ci] || ce[ci])
                        IOLog("  [0x%02x] 0x%08x (sys: 0x%08x %s)\n",
                              ci*4, ve[ci], ce[ci],
                              ve[ci] == ce[ci] ? "==" : "!=");
            }
        }

        // Configure IRQ routing for RISC-V doorbell (como no SEC2 trampoline path)
        writeReg32(FALCON_IRQSCLR, 0xFFFFFFFF);
        __sync_synchronize();
        writeReg32(FALCON_IRQMSET, 0x40);
        __sync_synchronize();
        writeReg32(FALCON_IRQDEST, 0x00);  // route ALL IRQs to RISC-V (not HOST)
        __sync_synchronize();
        writeAbsReg32(NV_PRISCV_RISCV_IRQDEST, 0x00); // RISC-V IRQDEST: route to core
        __sync_synchronize();
        writeReg32(NV_PGSP_RISCV_IRQMASK_REL, 0x40); // RISCV_IRQMASK (Ampere GSP RISC-V)
        __sync_synchronize();
        writeReg32(NV_PGSP_RISCV_ITCMSK, 0x40); // desmascarar IRQ #6 no RISC-V core
        __sync_synchronize();

        // Debug H2: check IRQSTAT before and after doorbell
        uint32_t irq_before = readReg32(FALCON_IRQSTAT);    // using aliased offset (0x0008)
        setProperty("GA104_IRQSTAT_before", irq_before, 32);
        IOLog("GA104: IRQSTAT before doorbell=0x%08x\n", irq_before);

        // Doorbell: NVIDIA usa QUEUE_HEAD = 0 (pulse, valor não importa)
        writeReg32(GSP_DOORBELL_REL, 0);
        __sync_synchronize();
        IOLog("GA104: Doorbell sent (pulse)\n");
        uint32_t irq_after = readReg32(FALCON_IRQSTAT);     // using aliased offset (0x0008)
        setProperty("GA104_IRQSTAT_after", irq_after, 32);
        IOLog("GA104: IRQSTAT after doorbell=0x%08x (delta=0x%08x)\n", irq_after, irq_after ^ irq_before);
        IOLog("GA104: Polling sysmem cmdq for 60s...\n");
        uint32_t finalWp = 0, finalRp = 0;

        // VRAM access test — safe, not in page table area (fVramLayout.queuePhysAddr)
        if (fBar1Virt) {
            volatile uint32_t *vramLow = (volatile uint32_t*)(fBar1Virt + 0x100);
            __sync_synchronize(); *vramLow = 0xCAFEBABE; __sync_synchronize();
            uint32_t v2 = *vramLow;
            IOLog("GA104: VRAM test @0x100: wrote 0xCAFEBABE read 0x%08x %s\n",
                  v2, v2 == 0xCAFEBABE ? "✅" : "❌");
            setProperty("GA104_VRAM_Low", v2, 32);
        }
        // Read MAILBOX0 for firmware status
        uint32_t mb0_val = readReg32(FALCON_MAILBOX0);  
        uint32_t cpuctl = readReg32(FALCON_CPUCTL);   
        uint32_t bcr    = readReg32(FALCON_BCR_CTRL);   
        IOLog("GA104: GSP mailbox0=0x%08x cpuctl=0x%08x bcr=0x%08x\n",
              mb0_val, cpuctl, bcr);
        setProperty("GA104_MB0", mb0_val, 32);
        setProperty("GA104_CPUCTL", cpuctl, 32);
        setProperty("GA104_BCR", bcr, 32);
        
        // If MB0 looks like an address, try reading from it via VRAM
        if (mb0_val > 0x100000 && mb0_val < fVRAMSize && fBar1Virt) {
            volatile uint32_t *fw_mem = (volatile uint32_t*)(fBar1Virt + mb0_val);
            fWprAddr = mb0_val; // Save for later polling
            IOLog("GA104: FW mem dump @0x%08x:\n", mb0_val);
            for (int di = 0; di < 32; di++) {
                uint32_t v = fw_mem[di];
                if (v) IOLog("  [0x%02x] 0x%08x\n", di*4, v);
            }
            setProperty("GA104_FWmem10", fw_mem[0x10/4], 32);
            setProperty("GA104_FWmem20", fw_mem[0x20/4], 32);
            fLastFwVal = fw_mem[0]; // Initial value for polling
        }

        // Poll MSGQ for GSP_INIT_DONE event (firmware sends this after processing init RPCs)
        // Follows NVIDIA kgspWaitForRmInitDone pattern
        IOLog("GA104: Waiting for GSP_INIT_DONE (60s)...\n");
        bool initDone = false;
        for (int waitMs = 0; waitMs < 60000; waitMs += 100) {
            IOSleep(100);
            if (!fShmBuf) continue;
            
            uint32_t cmdqRp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x20);
            // Poll MSGQ writePtr — firmware writes events here after processing
            uint32_t msgqWp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + 0x10);

            if ((waitMs % 5000) == 0)
                IOLog("GA104: poll %dms: cmdq rPtr=%u msgq wPtr=%u\n", waitMs, cmdqRp, msgqWp);

            if (msgqWp > fLastMsgqRp) {
                for (uint32_t ei = fLastMsgqRp; ei < msgqWp; ei++) {
                    uint32_t eIdx = ei % GSP_QUEUE_MSG_COUNT;
                    uint8_t *entry = fMsgqEntryBase + eIdx * GSP_QUEUE_MSG_SIZE;
                    GspMsgQueuePrefix *pre = (GspMsgQueuePrefix*)entry;
                    GspRpcMessageHeader *rpc = (GspRpcMessageHeader*)(entry + sizeof(GspMsgQueuePrefix));
                    IOLog("GA104: MSGQ[%u] seq=%u func=0x%04x result=0x%08x\n",
                          ei, pre->seqNum, rpc->function, rpc->rpcResult);
                    if (rpc->function == NV_VGPU_MSG_EVENT_GSP_INIT_DONE) {
                        IOLog("GA104: GSP_INIT_DONE received! ✅ Firmware ready!\n");
                        setProperty("GA104_GSP_INIT_DONE", true);
                        initDone = true;
                        break;
                    }
                }
                fLastMsgqRp = msgqWp;
                if (initDone) break;
            }
            
            // Log cmdq readPtr progress but DON'T break — INIT_DONE comes after RM init
            if (cmdqRp > 0 && (waitMs % 5000) == 0)
                IOLog("GA104: CMDQ rPtr=%u (waiting for INIT_DONE)\n", cmdqRp);
        }
        setProperty("GA104_GSP_INIT_DONE_recv", initDone);
        
        // Read final state
        if (fShmBuf) {
            finalRp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x20);
            finalWp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x10);
        }
        IOLog("GA104: Final sysmem cmdq: wPtr=%u rPtr=%u\n", finalWp, finalRp);
        setProperty("GA104_FinalwPtr", finalWp, 32);
        setProperty("GA104_FinalrPtr", finalRp, 32);

        IOLog("GA104: DEBUG calling sendGspRpcAllocRoot()\n");
        return sendGspRpcAllocRoot();
    }

    if (hasBooter) {
        IOLog("GA104: SEC2 failed, trying GSP Falcon booter\n");
        setProperty("GA104_GSPFalconFallback", true);

        // Load booter to GSP Falcon
        if (fWprMetaBuf) memset(fWprMetaBuf, 0, GSP_FW_WPR_META_SIZE);
        populateWprMeta();
        IOReturn blRet = loadBLtoFalcon();
        if (blRet != kIOReturnSuccess) return blRet;
        IOReturn sbRet = gspStartBooter();
        if (sbRet != kIOReturnSuccess) return sbRet;
        IOLog("GA104: GSP Falcon booter done\n");
        fGSPBooted = true;

        writeReg32(FALCON_OS, 0x00010001);
        __sync_synchronize();
        writeReg32(FALCON_MAILBOX0, (uint32_t)(fLibosPhys & 0xFFFFFFFF));
        writeReg32(FALCON_MAILBOX1, (uint32_t)((fLibosPhys >> 32) & 0xFFFFFFFF));
        __sync_synchronize();

        // Link MSGQ
        if (fShmBuf) {
            volatile uint32_t *mqRp = (volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + 0x20);
            *mqRp = 0; __sync_synchronize();
            bool linked = false;
            for (int r = 0; r < 40; r++) {
                IOSleep(100);
                uint32_t wp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + 0x10);
                uint32_t rp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x20);
                if (wp > 0 || rp != 0xFFFFFFFF) { linked = true; break; }
            }
            IOLog("GA104: MSGQ linked: %s\n", linked ? "yes" : "timeout");
            setProperty("GA104_MSGQ_Linked", linked);
        }

        // Pre-boot RPCs
        if (fGSPProtocol && fCmdqTx && fBar1Phys) {
            IOLog("GA104: Pre-boot RPCs...\n");
            uint32_t wp = fCmdqTx->writePtr;
            auto qEntry = [&](GspRpcMessageHeader *hdr, const void *pay, uint32_t paySz) {
                uint8_t elem[GSP_QUEUE_MSG_SIZE]; bzero(elem, sizeof(elem));
                GspMsgQueuePrefix *pre = (GspMsgQueuePrefix*)elem;
                pre->mctpHeader = GSP_MCTP_HEADER_SINGLE;
                pre->nvdmHeader = GSP_NVDM_HEADER_RM_RPC;
                memcpy(elem + sizeof(GspMsgQueuePrefix), hdr, sizeof(GspRpcMessageHeader));
                if (pay && paySz)
                    memcpy(elem + sizeof(GspMsgQueuePrefix) + sizeof(GspRpcMessageHeader), pay, paySz);
                pre->seqNum = wp;
                uint64_t cs = 0;
                for (uint32_t ci = 0; ci < sizeof(elem) / 8; ci++) cs ^= ((uint64_t*)elem)[ci];
                pre->checksum = (uint32_t)((cs >> 32) ^ (cs & 0xFFFFFFFF));
                uint32_t qIdx = wp % GSP_QUEUE_MSG_COUNT;
                uint32_t copySz = sizeof(GspMsgQueuePrefix) + sizeof(GspRpcMessageHeader) + paySz;
                memcpy(fCmdqEntryBase + qIdx * GSP_QUEUE_MSG_SIZE, elem, copySz);
                wp++;
            };
            GspRpcMessageHeader siMsg;
            fGSPProtocol->buildSetSystemInfo(&siMsg, fBar0Phys, fBar1Phys, fBar2Phys, fDeviceID, 0, fRevision, fBDF);
            qEntry(&siMsg, (uint8_t*)&siMsg + sizeof(GspRpcMessageHeader), siMsg.length - sizeof(GspRpcMessageHeader));
            uint32_t regBuf[256]; uint32_t regLen = sizeof(regBuf);
            GspRpcMessageHeader rgMsg;
            fGSPProtocol->buildSetRegistry(&rgMsg, regBuf, &regLen);
            qEntry(&rgMsg, regBuf, regLen);
            fCmdqTx->writePtr = wp;
            *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x10) = wp;
            __sync_synchronize();
            IOLog("GA104: Pre-boot RPCs done (%u entries)\n", wp);
        }

        // Doorbell: NVIDIA usa QUEUE_HEAD = 0 (pulse)
        writeReg32(GSP_DOORBELL_REL, 0);
        __sync_synchronize();
        IOLog("GA104: Doorbell sent (pulse)\n");

        // Poll for firmware response (60s)
        IOLog("GA104: Polling for INIT_DONE (60s)...\n");
        bool initDone = false;
        for (int ms = 0; ms < 60000; ms += 100) {
            IOSleep(100);
            if (!fShmBuf) continue;
            uint32_t mwp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + 0x10);
            if (mwp > 0) {
                for (uint32_t ei = 0; ei < mwp; ei++) {
                    GspRpcMessageHeader *rpc = (GspRpcMessageHeader*)(fMsgqEntryBase + (ei % GSP_QUEUE_MSG_COUNT) * GSP_QUEUE_MSG_SIZE + sizeof(GspMsgQueuePrefix));
                    if (rpc->function == NV_VGPU_MSG_EVENT_GSP_INIT_DONE) {
                        IOLog("GA104: INIT_DONE! ✅\n");
                        initDone = true; break;
                    }
                }
                if (initDone) break;
            }
        }
        setProperty("GA104_GSP_INIT_DONE_recv", initDone);
        uint32_t frp = 0, fwp = 0;
        if (fShmBuf) { frp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x20); fwp = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x10); }
        IOLog("GA104: Final cmdq wPtr=%u rPtr=%u\n", fwp, frp);
        setProperty("GA104_FinalwPtr", fwp, 32);
        setProperty("GA104_FinalrPtr", frp, 32);
        return sendGspRpcAllocRoot();
    }

    return kIOReturnTimeout;
}
IOReturn GA104Device::calculateVramLayout()
{
    if (!fVRAMSize) {
        IOLog("GA104: No VRAM size for layout calculation\n");
        return kIOReturnNotReady;
    }

    // Bottom-up VRAM layout: [Heap | Boot | ELF] from bottom, [VGA Workspace] at top
    //          ↑ Bottom of VRAM          ↑ Top of VRAM

    uint64_t fbSize = fVRAMSize;
    uint64_t vgaWkAddr = fbSize - 0x100000; // Last 1MB for VGA BIOS
    uint64_t frtsSize = 0;                  // FRTS not needed (direct boot)
    uint64_t frtsAddr = vgaWkAddr;

    // Bootloader binary in WPR2 (from bootloader size or 0x100000)
    uint64_t bootSize = fBootloaderSize ? fBootloaderSize : 0x100000;
    bootSize = (bootSize + 0xFFF) & ~0xFFF;
    uint64_t bootAddr = (frtsAddr - bootSize) & ~0xFFF;

    // Main firmware ELF .fwimage size
    uint32_t fwImgSize = 0;
    if (fGSPFirmware) {
        fGSPFirmware->getFWImage(&fwImgSize);
    }
    uint64_t elfSize = fwImgSize ? fwImgSize : GA104_GSP_FW_SIZE;
    elfSize = (elfSize + 0xFFFF) & ~0xFFFF; // Align 64K
    uint64_t elfAddr = (bootAddr - elfSize) & ~0xFFFF;

    // Heap in WPR2
    uint64_t heapSize = 0x200000; // 2MB (small heap for constrained BAR1)
    uint64_t heapAddr = (elfAddr - heapSize) & ~0xFFFFF; // Align 1MB

    // WPR2 starts at heap base, ends at VGA workspace base
    uint64_t wpr2Addr = heapAddr;
    uint64_t wpr2Size = (frtsAddr + frtsSize) - heapAddr;

    // Non-WPR heap (1MB below WPR2, only if space permits)
    uint64_t nwprHeapSize = 0;

    // If VRAM is very small, pack things tightly
    if (heapAddr < 0x100000) {
        IOLog("GA104: VRAM too small for standard layout, packing tightly\n");
        heapSize = 0x40000; // 256KB minimum heap
        heapAddr = 0x100000; // Start at 1MB from base
        elfAddr = heapAddr + heapSize;
        bootAddr = elfAddr + elfSize;
        frtsAddr = bootAddr + bootSize;
        vgaWkAddr = fbSize - 0x100000;
        // Recalculate WPR2
        wpr2Addr = heapAddr;
        wpr2Size = vgaWkAddr - heapAddr;
    }

    fVramLayout.wpr2Addr = wpr2Addr;
    fVramLayout.wpr2Size = wpr2Size;
    fVramLayout.heapAddr = heapAddr;
    fVramLayout.heapSize = heapSize;
    fVramLayout.elfAddr = elfAddr;
    fVramLayout.elfSize = (uint64_t)elfSize;
    fVramLayout.bootAddr = bootAddr;
    fVramLayout.bootSize = bootSize;
    fVramLayout.frtsAddr = frtsAddr;
    fVramLayout.frtsSize = frtsSize;
    fVramLayout.fbSize = fbSize;

    // Reserve queue space in top of WPR2 heap (last 256KB = 2 × 128KB)
    // Firmware page tables already cover the heap → firmware can access queues!
    fVramLayout.queueOffset = heapSize - 0x40000; // 0x1C0000
    fVramLayout.queuePhysAddr = heapAddr + fVramLayout.queueOffset;

    fFwEntryPoint = 0;

    IOLog("GA104: VRAM layout fb=0x%llx wpr=[0x%llx-0x%llx) elf=0x%llx boot=0x%llx frts=0x%llx heap=0x%llx queues=0x%llx\n",
          fbSize, wpr2Addr, frtsAddr + frtsSize, elfAddr, bootAddr, frtsAddr, heapAddr,
          fVramLayout.queuePhysAddr);

    setProperty("GA104P2_VramLayout", true);
    setProperty("GA104P2_Wpr2Addr", wpr2Addr, 64);
    setProperty("GA104P2_ElfAddr", elfAddr, 64);
    setProperty("GA104P2_QueuePhys", fVramLayout.queuePhysAddr, 64);
    return kIOReturnSuccess;
}
IOReturn GA104Device::buildRadix3PageTable(uint32_t fwSize)
{
    if (fRadix3Buf) return kIOReturnSuccess;

    // Radix3 is a 3-level page table (L0, L1, L2) mapping the firmware ELF
    // in system memory so the SEC2 bootloader can DMA it into WPR2.
    //
    // Each entry is 8 bytes. L0 has 1 entry pointing to L1.
    // L1 has N entries pointing to L2 pages.
    // L2 has M entries pointing to 4K pages of the ELF.
    //
    // For a 72MB ELF: 72MB / 4K = 18432 pages
    // L2 entries per page: 4096/8 = 512
    // L2 pages: ceil(18432/512) = 37 pages
    // L1 entries: 37 (pointing to each L2 page)
    // L1 pages: ceil(37/512) = 1 page
    // L0: 1 entry → 1 page

    uint32_t fwPages = (fwSize + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE;
    uint32_t l2PerPage = GSP_QUEUE_PAGE_SIZE / 8; // 512 entries per page
    uint32_t l2Pages = (fwPages + l2PerPage - 1) / l2PerPage;
    uint32_t l1Pages = (l2Pages + l2PerPage - 1) / l2PerPage;
    uint32_t totalBytes = GSP_QUEUE_PAGE_SIZE + l1Pages * GSP_QUEUE_PAGE_SIZE + l2Pages * GSP_QUEUE_PAGE_SIZE;

    fRadix3Buf = IOMallocAligned(totalBytes, GSP_QUEUE_PAGE_SIZE);
    if (!fRadix3Buf) return kIOReturnNoMemory;

    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
        (mach_vm_address_t)fRadix3Buf, totalBytes, kIODirectionInOut, kernel_task);
    if (md) { md->prepare(); fRadix3Phys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
    if (!fRadix3Phys) { IOFreeAligned(fRadix3Buf, totalBytes); fRadix3Buf = nullptr; return kIOReturnNoMemory; }

    uint64_t *l0 = (uint64_t*)fRadix3Buf;
    uint64_t *l1 = (uint64_t*)((uint8_t*)fRadix3Buf + GSP_QUEUE_PAGE_SIZE);
    uint64_t *l2 = (uint64_t*)((uint8_t*)l1 + l1Pages * GSP_QUEUE_PAGE_SIZE);

    // L0 → L1
    l0[0] = fRadix3Phys + GSP_QUEUE_PAGE_SIZE;

    // L1 → each L2 page
    for (uint32_t i = 0; i < l2Pages; i++) {
        l1[i] = fRadix3Phys + GSP_QUEUE_PAGE_SIZE + l1Pages * GSP_QUEUE_PAGE_SIZE + i * GSP_QUEUE_PAGE_SIZE;
    }

    // L2 → firmware data pages (physical addresses of the firmware buffer)
    uint64_t fwBufPhys = fFWBufferPhys;
    for (uint32_t i = 0; i < fwPages; i++) {
        l2[i] = fwBufPhys + i * GSP_QUEUE_PAGE_SIZE;
    }

    __sync_synchronize();

    IOLog("GA104: Radix3 built: fwPages=%u l2Pages=%u totalPhys=0x%llx\n",
          fwPages, l2Pages, fRadix3Phys);
    return kIOReturnSuccess;
}
IOReturn GA104Device::setupWpr2()
{
    if (!fVramLayout.wpr2Addr || !fVramLayout.wpr2Size)
        return kIOReturnNotReady;

    writeAbsReg32(NV_PFB_PRI_MMU_WPR2_ADDR_LO, (uint32_t)(fVramLayout.wpr2Addr & 0xFFFFFFFF));
    uint32_t hiCheck = writeAbsReg32(NV_PFB_PRI_MMU_WPR2_ADDR_HI,
        (uint32_t)((fVramLayout.wpr2Addr >> 32) & 0x7FFFFFFF) | 0x80000000);
    IOLog("GA104: WPR2 set: addr=0x%llx size=0x%llx hiCheck=0x%08x\n",
          fVramLayout.wpr2Addr, fVramLayout.wpr2Size, hiCheck);

    setProperty("GA104P2_Wpr2Set", true);
    setProperty("GA104P2_Wpr2Check", hiCheck, 32);
    return kIOReturnSuccess;
}
void GA104Device::cleanupPhase2()
{
    if (fWprMetaBuf) { IOFreeAligned(fWprMetaBuf, GSP_FW_WPR_META_SIZE); fWprMetaBuf = nullptr; }

    if (fSec2SigBuf) { IOFreeAligned(fSec2SigBuf, gSec2SigSize); fSec2SigBuf = nullptr; }
    if (fRadix3Buf) {
        uint32_t fwPages = (fVramLayout.elfSize + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE;
        uint32_t l2PerPage = GSP_QUEUE_PAGE_SIZE / 8;
        uint32_t l2Pages = (fwPages + l2PerPage - 1) / l2PerPage;
        uint32_t l1Pages = (l2Pages + l2PerPage - 1) / l2PerPage;
        uint32_t totalBytes = GSP_QUEUE_PAGE_SIZE + l1Pages * GSP_QUEUE_PAGE_SIZE + l2Pages * GSP_QUEUE_PAGE_SIZE;
        IOFreeAligned(fRadix3Buf, totalBytes);
        fRadix3Buf = nullptr;
    }
    if (fLibosBuf) { IOFreeAligned(fLibosBuf, GSP_QUEUE_PAGE_SIZE); fLibosBuf = nullptr; }
    if (fLogInitBuf) { IOFreeAligned(fLogInitBuf, 0x10000); fLogInitBuf = nullptr; }
    if (fLogIntrBuf) { IOFreeAligned(fLogIntrBuf, 0x10000); fLogIntrBuf = nullptr; }
    if (fLogRmBuf) { IOFreeAligned(fLogRmBuf, 0x10000); fLogRmBuf = nullptr; }
    if (fRmargsBuf) { IOFreeAligned(fRmargsBuf, GSP_QUEUE_PAGE_SIZE); fRmargsBuf = nullptr; }
    if (fShmBuf) {
        uint32_t cmdqPages = GSP_QUEUE_SIZE / GSP_QUEUE_PAGE_SIZE;
        uint32_t pteEntries = cmdqPages * 2;
        uint32_t ptePages = (pteEntries * 8 + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE;
        pteEntries += ptePages;
        uint32_t pteSize = ((pteEntries * 8) + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE * GSP_QUEUE_PAGE_SIZE;
        uint32_t shmSize = pteSize + GSP_QUEUE_SIZE * 2;
        IOFreeAligned(fShmBuf, shmSize);
        fShmBuf = nullptr;
    }
    fWprMetaPhys = 0; fRadix3Phys = 0; fLibosPhys = 0; fShmPhys = 0;
    fLogInitPhys = 0; fLogIntrPhys = 0; fLogRmPhys = 0; fRmargsPhys = 0;
}

