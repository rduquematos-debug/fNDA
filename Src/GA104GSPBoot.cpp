// GA104GSPBoot.cpp — GA104Device display implementation
#include "GA104Device.hpp"
#include "SEC2Embed_Image.h"
#include "SEC2Embed_HsBlSig.h"
#include "SEC2Embed_Sig.h"
#include "GA104DeviceUtilities.h"
#include "GA104Regs.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <string.h>

#define super IOService


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

    // SEC2 Falcon base register aliases
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
    uint32_t gspCpuctl1 = readReg32(FALCON_CPUCTL);
    uint32_t mailbox0 = readReg32(FALCON_MAILBOX0);
    setProperty("GA104GSP_Step1_CPUCTL", gspCpuctl1, 32);
    setProperty("GA104GSP_Step1_Mailbox0", mailbox0, 32);
    IOLog("GA104: GSP CPUCTL=0x%08x mb0=0x%x\n", gspCpuctl1, mailbox0);
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

        // SEC2 booted the GSP; ensure VRAM cmdq readPtr is reset to 0
        if (fVramCmdqEntryBase) {
            uint32_t *vramCmdqReadPtr = (uint32_t*)((fVramCmdqEntryBase - GSP_QUEUE_PAGE_SIZE)) + 0x20 / 4;
            *vramCmdqReadPtr = 0;
            __sync_synchronize();
            IOLog("GA104: SEC2: VRAM cmdq readPtr reset to 0\n");
        }

         if (!(gspCpuctl & 0x10)) {
             // CASO A: GSP already running — firmware loaded by SEC2 in WPR2.
             IOLog("GA104: GSP running after SEC2! Proceeding to ALLOC_ROOT test.\n");
             fGSPBooted = true;
             writeReg32(FALCON_OS, 0x00010001);  // app version
             __sync_synchronize();
        } else {
            // CASO C: GSP halted after SEC2 — per NVIDIA, this is an error
            IOLog("GA104: GSP halted after SEC2, aborting boot\n");
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
        uint32_t gspCpuctlAfter = readReg32(FALCON_CPUCTL);   
        uint32_t bcr            = readReg32(FALCON_BCR_CTRL);   
        IOLog("GA104: GSP mailbox0=0x%08x cpuctl=0x%08x bcr=0x%08x\n",
              mb0_val, gspCpuctlAfter, bcr);
        setProperty("GA104_CPUCTL", gspCpuctlAfter, 32);
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
        IOReturn rootRet = sendGspRpcAllocRoot();
        if (rootRet == kIOReturnSuccess) {
            IOLog("GA104: ALLOC_ROOT OK, allocating display chain...\n");
            sendGspRpcAllocDisplayChain();
        }
        return rootRet;
    }

    // SEC2 failed — try GSP Falcon booter (if bootloader available)
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
        IOReturn rootRet2 = sendGspRpcAllocRoot();
        if (rootRet2 == kIOReturnSuccess) {
            IOLog("GA104: ALLOC_ROOT OK (2), allocating display chain...\n");
            sendGspRpcAllocDisplayChain();
        }
        return rootRet2;
    }

    return kIOReturnTimeout;
}


// --- Write RISC-V CSR via Falcon ICD interface ---
// --- Read GSP RISC-V CSRs via Falcon ICD interface ---
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


IOReturn GA104Device::loadGSPFirmware()
{
    fGSPFirmware = new GSPFirmware;
    if (!fGSPFirmware) return kIOReturnNoMemory;
    if (!fGSPFirmware->init()) { OSSafeReleaseNULL(fGSPFirmware); return kIOReturnUnsupported; }
    IOLog("GA104: GSPFirmware created\n");
    return kIOReturnSuccess;
}


IOReturn GA104Device::createFWBuffer(uint32_t size)
{
    if (size == 0 || size > GA104_GSP_FW_SIZE) return kIOReturnBadArgument;
    if (fFWBuffer) { IOFreeAligned(fFWBuffer, fFWBufferSize); fFWBuffer = nullptr; }
    void *buf = IOMallocAligned(size, PAGE_SIZE);
    if (!buf) { IOLog("GA104: IOMallocAligned(%u) failed\n", size); return kIOReturnNoMemory; }
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)buf, size, kIODirectionInOut, kernel_task);
    uint64_t pa = 0;
    if (md) { md->prepare(); pa = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
    if (!pa) { IOFreeAligned(buf, size); return kIOReturnNoMemory; }
    fFWBuffer = buf; fFWBufferPhys = pa; fFWBufferSize = size;
    IOLog("GA104: FW buf %u bytes virt=%p phys=0x%llx\n", size, buf, pa);
    return kIOReturnSuccess;
}


IOReturn GA104Device::createBLBuffer(uint32_t size)
{
    if (size == 0 || size > 0x100000) return kIOReturnBadArgument;
    if (fBootloaderBuffer) { IOFreeAligned(fBootloaderBuffer, fBootloaderSize); fBootloaderBuffer = nullptr; }
    void *buf = IOMallocAligned(size, PAGE_SIZE);
    if (!buf) { IOLog("GA104: BL alloc %u failed\n", size); return kIOReturnNoMemory; }
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange((mach_vm_address_t)buf, size, kIODirectionInOut, kernel_task);
    uint64_t pa = 0;
    if (md) { md->prepare(); pa = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
    if (!pa) { IOFreeAligned(buf, size); return kIOReturnNoMemory; }
    fBootloaderBuffer = buf; fBootloaderPhys = pa; fBootloaderSize = size;
    IOLog("GA104: BL buf %u bytes virt=%p phys=0x%llx\n", size, buf, pa);
    return kIOReturnSuccess;
}




// === Restored functions for GSP Falcon booter ===
IOReturn GA104Device::loadBLtoFalcon(void)
{
    if (!fBootloaderBuffer || fBootloaderSize == 0) return kIOReturnNotReady;

    GspBootInfo bl = {};

    // Try booter v3 format first (encrypted), fall back to legacy HSv2/RmUcode
    bool parsed = parseBooterLoadV3((const uint8_t*)fBootloaderBuffer, fBootloaderSize, &bl);

    if (!parsed) {
        // Fallback: parse as legacy HSv2/RmUcode bootloader
        if (!parseGspBootloader((const uint8_t*)fBootloaderBuffer, fBootloaderSize, &bl)) {
            IOLog("GA104: Failed to parse bootloader (any format)\n");
            return kIOReturnBadArgument;
        }
    }

    if (!bl.imem_src || bl.imem_size == 0) {
        IOLog("GA104: Invalid bootloader IMEM\n");
        return kIOReturnBadArgument;
    }

    IOLog("GA104: Loading bootloader to Falcon: IMEM=%u DMEM=%u enc=%d v3=%d engMask=0x%x ucode=%u\n",
          bl.imem_size, bl.dmem_size, bl.is_encrypted, bl.is_booter_v3,
          bl.engine_id_mask, bl.ucode_id);

    // BCR_CTRL = 0 (Falcon mode for DMA)
    writeReg32(FALCON_BCR_CTRL, 0);
    __sync_synchronize();

    // Reset Falcon before loading
    uint32_t engine = readReg32(FALCON_ENGINE);
    writeReg32(FALCON_ENGINE, engine | 0x01);
    IODelay(10);
    writeReg32(FALCON_ENGINE, engine & ~0x01);
    IODelay(10);
    for (int i = 0; i < 1000; i++) {
        if (!(readReg32(FALCON_HWCFG2) & FALCON_HWCFG2_MEM_SCRUBBING)) break;
        IODelay(1);
    }
    // DMA IMEM to Falcon using DMATRF
    uint64_t blBasePhys = fBootloaderPhys;
    uint32_t imemOff = (uint32_t)((uintptr_t)bl.imem_src - (uintptr_t)fBootloaderBuffer);
    uint32_t dmemOff = (bl.dmem_src) ? (uint32_t)((uintptr_t)bl.dmem_src - (uintptr_t)fBootloaderBuffer) : 0;
    uint64_t imemPhys = blBasePhys + imemOff;
    uint64_t dmemPhys = (bl.dmem_src) ? (blBasePhys + dmemOff) : 0;

    IOLog("GA104: Falcon DMA: blBasePhys=0x%llx imemOff=0x%x dmemOff=0x%x\n",
          blBasePhys, imemOff, dmemOff);

    // IMEM DMA: split into 32KB chunks (max DMATRF size)
    const uint32_t maxDma = 32768;
    uint32_t imemRemaining = bl.imem_size;
    uint32_t imemDone = 0;
    while (imemRemaining > 0) {
        uint32_t chunk = (imemRemaining > maxDma) ? maxDma : imemRemaining;
        uint64_t chunkPhys = imemPhys + imemDone;
        writeReg32(FALCON_DMATRFBASE, (uint32_t)(chunkPhys & 0xFFFFFFFF));
        writeReg32(FALCON_DMATRFBASE1, (uint32_t)((chunkPhys >> 32) & 0xFFFFFFFF));
        writeReg32(FALCON_DMATRFMOFFS, imemDone);
        writeReg32(FALCON_DMATRFFBOFFS, 0);
        writeReg32(FALCON_DMATRFCMD, 0x5 | dma_size_encoding(chunk));
        __sync_synchronize();
        for (int i = 0; i < 1000; i++) {
            if (readReg32(FALCON_DMATRFCMD) & 0x2) break;
            IODelay(1);
        }
        imemDone += chunk;
        imemRemaining -= chunk;
        IOLog("GA104: IMEM chunk %u/%u done\n", imemDone, bl.imem_size);
    }
    IOLog("GA104: IMEM DMA done (%u bytes)\n", bl.imem_size);

    // DMA DMEM to Falcon (may be empty/zeros for booter v3)
    if (bl.dmem_src && bl.dmem_size > 0) {
        writeReg32(FALCON_DMATRFBASE, (uint32_t)(dmemPhys & 0xFFFFFFFF));
        writeReg32(FALCON_DMATRFBASE1, (uint32_t)((dmemPhys >> 32) & 0xFFFFFFFF));
        writeReg32(FALCON_DMATRFMOFFS, 0);
        writeReg32(FALCON_DMATRFFBOFFS, 0);
        writeReg32(FALCON_DMATRFCMD, 0x1 | dma_size_encoding(bl.dmem_size));
        __sync_synchronize();

        for (int i = 0; i < 1000; i++) {
            if (readReg32(FALCON_DMATRFCMD) & 0x2) break;
            IODelay(1);
        }
        IOLog("GA104: DMEM DMA done (%u bytes)\n", bl.dmem_size);
    }

    setProperty("GA104BL_ImemSz", bl.imem_size, 32);
    setProperty("GA104BL_DmemSz", bl.dmem_size, 32);
    setProperty("GA104BL_EngMask", bl.engine_id_mask, 32);
    setProperty("GA104BL_Ucode", bl.ucode_id, 32);
    setProperty("GA104BL_Manifest", bl.manifest_offset, 32);
    setProperty("GA104BL_Encrypted", bl.is_encrypted ? 1 : 0, 8);
    setProperty("GA104BL_V3", bl.is_booter_v3 ? 1 : 0, 8);

    // Re-parse and store for gspStartBooter
    fBooterV3 = bl.is_booter_v3;
    if (bl.is_booter_v3) {
        fBooterImemSz = bl.imem_size;
        fBooterDmemSz = bl.dmem_size;
        fBooterManifestOff = bl.manifest_offset;
        fBooterEngMask = bl.engine_id_mask;
        fBooterUcode = bl.ucode_id;
        fBooterAppVer = bl.app_version ? bl.app_version : 0x00010001;
    }

    return kIOReturnSuccess;
}


// SEC2 booter — follows NVIDIA kgspExecuteHsFalcon_GA102 exactly
// GA102/GA104: SEC2 bBootFromHs=TRUE, runs booter in Falcon mode (NO RISC-V on SEC2!)

IOReturn GA104Device::gspStartBooter(void)
{
    if (!fBooterV3) {
        IOLog("GA104: gspStartBooter: no booter v3 loaded\n");
        return kIOReturnNotReady;
    }

    IOLog("GA104: Starting Booter (BROM + CPUCTL)...\n");

    // === Step 1: Configure Falcon registers ===
    writeReg32(FALCON_DMACTL, 0);
    __sync_synchronize();
    writeReg32(FALCON_RM, 0x24820000);
    __sync_synchronize();

    // === Step 2: Enable Falcon interfaces ===
    writeReg32(FALCON_ITFEN, FALCON_ITFEN_CTXEN | FALCON_ITFEN_MTHDEN | FALCON_ITFEN_DTFEN);
    __sync_synchronize();
    writeReg32(FALCON_FBIF_CTL, 0x180);
    // TRANSCFG = _COHERENT_SYSMEM | _PHYSICAL (firmware reads cmdq/msgq from system memory)
    for (int fb = 0; fb < 4; fb++)
        writeReg32(FALCON_FBIF_TRANSCFG(0), 0x4);
        writeReg32(FALCON_FBIF_TRANSCFG(1), 0x4);
        writeReg32(FALCON_FBIF_TRANSCFG(2), 0x4);
        writeReg32(FALCON_FBIF_TRANSCFG(3), 0x4);
    __sync_synchronize();

    // === Step 3: Set BROM signature registers ===
    writeReg32(FALCON_BROM_PARAADDR, fBooterManifestOff);
    writeReg32(FALCON_BROM_ENGIDMASK, fBooterEngMask);
    writeReg32(FALCON_BROM_CURR_UCODE_ID, fBooterUcode);
    __sync_synchronize();
    IOLog("GA104: BROM set: PARADDR=0x%x ENGIDMASK=0x%x UCODE=%u\n",
          fBooterManifestOff, fBooterEngMask, fBooterUcode);

    writeReg32(FALCON_BROM_MOD_SEL, 1);
    __sync_synchronize();
    bool bromOk = false;
    for (int i = 0; i < 1000; i++) {
        if (!(readReg32(FALCON_BROM_MOD_SEL) & 1)) { bromOk = true; break; }
        IODelay(10);
    }
    IOLog("GA104: BROM MOD_SEL cleared (verification %s)\n", bromOk ? "OK" : "TIMEOUT");

    // === Step 4: Set MAILBOX0/1 = LibOS (firmware reads this) + SCRATCH = WPR Meta (booter) ===
    writeReg32(FALCON_MAILBOX0, (uint32_t)(fLibosPhys & 0xFFFFFFFF));
    writeReg32(FALCON_MAILBOX1, (uint32_t)((fLibosPhys >> 32) & 0xFFFFFFFF));
    __sync_synchronize();
    writeReg32(FALCON_SCRATCH0, (uint32_t)(fWprMetaPhys & 0xFFFFFFFF));
    writeReg32(FALCON_SCRATCH1, (uint32_t)((fWprMetaPhys >> 32) & 0xFFFFFFFF));
    __sync_synchronize();
    IOLog("GA104: MAILBOX=0x%llx (LibOS) SCRATCH=0x%llx (WPR Meta)\n", fLibosPhys, fWprMetaPhys);

    // === Step 5: Write FALCON_OS + BOOTVEC = WPR2 base ===
    writeReg32(FALCON_OS, fBooterAppVer);
    __sync_synchronize();
    // BOOTVEC = WPR2 base (Booter carregou firmware aqui)
    // O RISC-V core começa a executar daqui após Booter mudar para RISC-V mode
    uint32_t wpr2Base32 = (uint32_t)(fVramLayout.wpr2Addr & 0xFFFFFFFF);
    writeReg32(FALCON_BOOTVEC, wpr2Base32);
    // GA102+: RISC-V boot vector no register space secundário (NV_PFALCON2_GSP_RISCV_BOOTVECTOR)
    writeAbsReg32(0x00111000, wpr2Base32);
    __sync_synchronize();
    IOLog("GA104: BOOTVEC=0x%llx (WPR2 base) RISCV_BOOTVECTOR=0x%08x\n", fVramLayout.wpr2Addr, wpr2Base32);

    // === Step 6: Start the Booter Falcon ===
    IOLog("GA104: CPUCTL=0x02 (start Booter)...\n");
    writeReg32(FALCON_CPUCTL, 0x02);
    __sync_synchronize();

    // === Step 7: Poll for Booter completion ===
    readReg32(FALCON_BCR_CTRL); /* bcrStart discarded */
    bool booterDone = false;
    for (int i = 0; i < 5000; i++) {
        IOSleep(2);
        uint32_t bcr = readReg32(FALCON_BCR_CTRL);
        uint32_t booterCpuctl = readReg32(FALCON_CPUCTL);
        // Booter sets BCR_CTRL = CORE_SELECT_RISCV when switching to RISC-V
        // On GA104, VALID bit (0x1) may not be set by the Booter
        if ((bcr & 0x10) == 0x10) {
            IOLog("GA104: Booter done after %dms (BCR=0x%08x CPUCTL=0x%08x)\n",
                  i * 2, bcr, booterCpuctl);
            booterDone = true;
            break;
        }
        if ((i % 1000) == 999) {
            IOLog("GA104:   still waiting... BCR=0x%08x CPUCTL=0x%08x\n",
                  bcr, booterCpuctl);
        }
    }

    setProperty("GA104Booter_Done", booterDone);
    setProperty("GA104Booter_BCR", readReg32(FALCON_BCR_CTRL), 32);
    setProperty("GA104Booter_CPUCTL", readReg32(FALCON_CPUCTL), 32);

    if (!booterDone) {
        IOLog("GA104: Booter timeout! BCR_CTRL=0x%08x CPUCTL=0x%08x\n",
              readReg32(FALCON_BCR_CTRL), readReg32(FALCON_CPUCTL));
        uint32_t extAddr = readReg32(FALCON_EXTERRADDR);
        uint32_t extStat = readReg32(FALCON_EXTERRSTAT);
        IOLog("GA104: EXTERRADDR=0x%08x EXTERRSTAT=0x%08x\n", extAddr, extStat);
        return kIOReturnTimeout;
    }

    IOLog("GA104: Booter completed successfully, GSP-RM should be running\n");

    // After booter: GSP-RM is running in RISC-V mode
    // Set up MAILBOX for GSP-RM (LibOS address)
    writeReg32(FALCON_MAILBOX0, (uint32_t)(fLibosPhys & 0xFFFFFFFF));
    writeReg32(FALCON_MAILBOX1, (uint32_t)((fLibosPhys >> 32) & 0xFFFFFFFF));
    __sync_synchronize();

    return kIOReturnSuccess;
}


IOReturn GA104Device::gspSetupQueues()
{
    IOLog("GA104: gspSetupQueues starting\n");
    setProperty("GA104P2_Start", true);

    if (!fVramLayout.fbSize) {
        IOReturn ret = calculateVramLayout();
        if (ret != kIOReturnSuccess) return ret;
    }

    uint32_t fwImgSize = 0;
    uint8_t *fwImgData = fGSPFirmware ? fGSPFirmware->getFWImage(&fwImgSize) : nullptr;
    if (!fwImgData || fwImgSize == 0) {
        IOLog("GA104: No firmware image\n");
        return kIOReturnNotReady;
    }
    setProperty("GA104P2_FwSize", fwImgSize, 32);

    // Copy .fwimage to VRAM with boot-mode flag and WPR2 patches
    if (fVRAMBase && fVramLayout.elfAddr) {
        // Firmware patching handled by SEC2 booter internally
        // copy patched firmware to VRAM
        memcpy(fVRAMBase + fVramLayout.elfAddr, fwImgData, fwImgSize);
        __sync_synchronize();
        IOLog("GA104: .fwimage copied to VRAM at 0x%llx\n", fVramLayout.elfAddr);

        // Zero firmware BSS from ELF program headers (SEC2 bootloader normally does this)
        if (fGSPFirmware) {
            uint8_t *elf = fGSPFirmware->getFirmwareData();
            uint32_t elfSz = fGSPFirmware->getFirmwareSize();
            if (elf && elfSz >= 64 && elf[0] == 0x7f && elf[1] == 'E' && elf[2] == 'L' && elf[3] == 'F') {
                uint64_t e_phoff = *(uint64_t*)(elf + 0x20);
                uint16_t e_phentsize = *(uint16_t*)(elf + 0x36);
                uint16_t e_phnum = *(uint16_t*)(elf + 0x38);
                for (uint16_t i = 0; i < e_phnum; i++) {
                    uint8_t *ph = elf + e_phoff + i * e_phentsize;
                    if (*(uint32_t*)ph != 1) continue;
                    uint64_t p_paddr = *(uint64_t*)(ph + 0x18);
                    uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
                    uint64_t p_memsz = *(uint64_t*)(ph + 0x28);
                    if (p_memsz > p_filesz) {
                        uint64_t bssOff = p_paddr + p_filesz;
                        uint64_t bssSz = p_memsz - p_filesz;
                        if (fVRAMBase && bssOff + bssSz <= fVRAMSize) {
                            memset(fVRAMBase + bssOff, 0, bssSz);
                            __sync_synchronize();
                            IOLog("GA104: BSS zeroed: VRAM+0x%llx size=0x%llx\n", bssOff, bssSz);
                        }
                    }
                }
            }
        }

        // Zero the heap so RM allocates from clean VRAM (no stale pointers)
        if (fVramLayout.heapAddr && fVramLayout.heapSize) {
            memset(fVRAMBase + fVramLayout.heapAddr, 0, fVramLayout.heapSize);
            __sync_synchronize();
            IOLog("GA104: Heap zeroed at 0x%llx size 0x%llx\n", fVramLayout.heapAddr, fVramLayout.heapSize);
        }

        // Initialize firmware work queue idle state
        // The service loop at 0x6F0B6 checks ptr = mem[0x10076F50], then mem[ptr+8]
        // If mem[ptr+8] == -1, firmware idles properly instead of processing garbage
        if (fVRAMBase) {
            uint64_t *idleStruct = (uint64_t*)(fVRAMBase + 0x10085000);
            idleStruct[1] = 0xFFFFFFFFFFFFFFFFULL; // [ptr+8] = -1 = no work
            __sync_synchronize();
            uint64_t *queuePtr = (uint64_t*)(fVRAMBase + 0x10076F50);
            *queuePtr = 0x10085000; // point to idle structure
            __sync_synchronize();
            IOLog("GA104: Work queue initialized: ptr@0x10076F50 = 0x10085000\n");
        }

        setProperty("GA104P2_ImgCopied", true);
    }

    setupWpr2();

    // Allocate LibOS args buffer (4KB page)
    if (!fLibosBuf) {
        fLibosBuf = IOMallocAligned(GSP_QUEUE_PAGE_SIZE, GSP_QUEUE_PAGE_SIZE);
        if (!fLibosBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fLibosBuf, GSP_QUEUE_PAGE_SIZE, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fLibosPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fLibosPhys) { IOFreeAligned(fLibosBuf, GSP_QUEUE_PAGE_SIZE); fLibosBuf = nullptr; return kIOReturnNoMemory; }
    }

    // Allocate 3 log buffers (LOGINIT, LOGINTR, LOGRM) — 64KB each
    uint32_t logBufSize = 0x10000;
    if (!fLogInitBuf) {
        fLogInitBuf = IOMallocAligned(logBufSize, GSP_QUEUE_PAGE_SIZE);
        if (!fLogInitBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fLogInitBuf, logBufSize, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fLogInitPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fLogInitPhys) { IOFreeAligned(fLogInitBuf, logBufSize); fLogInitBuf = nullptr; return kIOReturnNoMemory; }
        // Write PTE array at offset 8 (offset 0 = put pointer = 0)
        memset(fLogInitBuf, 0, logBufSize);
        uint64_t *logPtes = (uint64_t*)((uint8_t*)fLogInitBuf + 8);
        for (uint32_t i = 0; i < logBufSize / GSP_QUEUE_PAGE_SIZE; i++)
            logPtes[i] = fLogInitPhys + i * GSP_QUEUE_PAGE_SIZE;
    }
    if (!fLogIntrBuf) {
        fLogIntrBuf = IOMallocAligned(logBufSize, GSP_QUEUE_PAGE_SIZE);
        if (!fLogIntrBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fLogIntrBuf, logBufSize, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fLogIntrPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fLogIntrPhys) { IOFreeAligned(fLogIntrBuf, logBufSize); fLogIntrBuf = nullptr; return kIOReturnNoMemory; }
        memset(fLogIntrBuf, 0, logBufSize);
        uint64_t *logPtes = (uint64_t*)((uint8_t*)fLogIntrBuf + 8);
        for (uint32_t i = 0; i < logBufSize / GSP_QUEUE_PAGE_SIZE; i++)
            logPtes[i] = fLogIntrPhys + i * GSP_QUEUE_PAGE_SIZE;
    }
    if (!fLogRmBuf) {
        fLogRmBuf = IOMallocAligned(logBufSize, GSP_QUEUE_PAGE_SIZE);
        if (!fLogRmBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fLogRmBuf, logBufSize, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fLogRmPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fLogRmPhys) { IOFreeAligned(fLogRmBuf, logBufSize); fLogRmBuf = nullptr; return kIOReturnNoMemory; }
        memset(fLogRmBuf, 0, logBufSize);
        uint64_t *logPtes = (uint64_t*)((uint8_t*)fLogRmBuf + 8);
        for (uint32_t i = 0; i < logBufSize / GSP_QUEUE_PAGE_SIZE; i++)
            logPtes[i] = fLogRmPhys + i * GSP_QUEUE_PAGE_SIZE;
    }

    // Allocate shared memory for queues — layout: [PTE array][cmdq][msgq] CONTIGUOUS
    // PTE count = (cmdqPages + msgqPages) + (PTE table pages for self-reference)
    uint32_t cmdqPages = GSP_QUEUE_SIZE / GSP_QUEUE_PAGE_SIZE;            // 256
    uint32_t msgqPages = GSP_QUEUE_SIZE / GSP_QUEUE_PAGE_SIZE;            // 256
    uint32_t pteEntries = cmdqPages + msgqPages;
    uint32_t ptePages = (pteEntries * 8 + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE;
    pteEntries += ptePages; // include PTEs for PTE pages themselves
    uint32_t pteSize = pteEntries * 8;
    uint32_t pteSizePages = (pteSize + GSP_QUEUE_PAGE_SIZE - 1) / GSP_QUEUE_PAGE_SIZE;
    pteSize = pteSizePages * GSP_QUEUE_PAGE_SIZE;
    uint32_t shmSize = pteSize + GSP_QUEUE_SIZE * 2; // PTEs + cmdq + msgq

    if (!fShmBuf) {
        fShmBuf = IOMallocAligned(shmSize, GSP_QUEUE_PAGE_SIZE);
        if (!fShmBuf) return kIOReturnNoMemory;
    }
    memset(fShmBuf, 0, shmSize);

    // Fill PTE array at offset 0 — physical addresses of EACH page (NVIDIA memdescGetPhysAddrs)
    // Must iterate segments because memory may be non-contiguous for large allocations
    uint64_t *shmPtes = (uint64_t*)fShmBuf;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
        (mach_vm_address_t)fShmBuf, shmSize, kIODirectionInOut, kernel_task);
    if (!md) return kIOReturnNoMemory;
    md->prepare();
    fShmPhys = md->getPhysicalSegment(0, nullptr);
    IOByteCount pteOff = 0;
    uint32_t pteIdx = 0;
    while (pteOff < shmSize && pteIdx < pteEntries) {
        IOByteCount segLen = 0;
        addr64_t segPhys = md->getPhysicalSegment(pteOff, &segLen);
        if (!segPhys || segLen == 0) break;
        uint32_t pages = (uint32_t)(segLen / GSP_QUEUE_PAGE_SIZE);
        for (uint32_t p = 0; p < pages && pteIdx < pteEntries; p++)
            shmPtes[pteIdx++] = segPhys + p * GSP_QUEUE_PAGE_SIZE;
        pteOff += segLen;
    }
    md->complete();
    md->release();
    if (!fShmPhys) { IOFreeAligned(fShmBuf, shmSize); fShmBuf = nullptr; return kIOReturnNoMemory; }

    // cmdq starts after PTE array
    fCmdqOff = (uint64_t)pteSize;
    fMsgqOff = fCmdqOff + GSP_QUEUE_SIZE;

    // cmdq header (host→GSP): use nvgMsgqTxCreate (NVIDIA msgq format)
    fCmdqTx = (GspMsgqTxHeader*)((uint8_t*)fShmBuf + fCmdqOff);
    nvgMsgqTxCreate(fCmdqTx, GSP_QUEUE_SIZE, GSP_QUEUE_MSG_SIZE, 2, 12, NVG_MSGQ_FLAGS_SWAP_RX);
    // Reset firmware's cmdq readPtr (at rxHdrOff from cmdq header base) to 0.
    *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + fCmdqTx->rxHdrOff) = 0;
    fCmdqEntryBase = (uint8_t*)fShmBuf + fCmdqOff + fCmdqTx->entryOff;
    IOLog("GA104: CMDQ: msgCount=%u entryOff=%u rxHdrOff=%u\n",
          fCmdqTx->msgCount, fCmdqTx->entryOff, fCmdqTx->rxHdrOff);

    // msgq header (GSP→host): initialized for firmware to use as its TX
    fMsgqTx = (GspMsgqTxHeader*)((uint8_t*)fShmBuf + fMsgqOff);
    nvgMsgqTxCreate(fMsgqTx, GSP_QUEUE_SIZE, GSP_QUEUE_MSG_SIZE, 2, 12, NVG_MSGQ_FLAGS_SWAP_RX);
    fMsgqEntryBase = (uint8_t*)fShmBuf + fMsgqOff + fMsgqTx->entryOff;
    fLastMsgqRp = 0;

    __sync_synchronize();
    setProperty("GA104P2_ShmPhys", fShmPhys, 64);

    // Populate LibOS args (5 entries: LOGINIT, LOGINTR, LOGRM, RMARGS, WPRMETA)
    LibosMemoryRegionInitArgument *libosArgs = (LibosMemoryRegionInitArgument*)fLibosBuf;
    memset(libosArgs, 0, GSP_QUEUE_PAGE_SIZE);

    // [0] LOGINIT  (LE bytes: 4C 4F 47 49 4E 49 54 00)
    libosArgs[0].id8 = 0x0054494E49474F4CULL;
    libosArgs[0].pa = fLogInitPhys;
    libosArgs[0].size = logBufSize;
    libosArgs[0].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
    libosArgs[0].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

    // [1] LOGINTR  (LE bytes: 4C 4F 47 49 4E 54 52 00)
    libosArgs[1].id8 = 0x0052544E49474F4CULL;
    libosArgs[1].pa = fLogIntrPhys;
    libosArgs[1].size = logBufSize;
    libosArgs[1].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
    libosArgs[1].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

    // [2] LOGRM  (LE bytes: 4C 4F 47 52 4D 00 00 00)
    libosArgs[2].id8 = 0x0000004D52474F4CULL;
    libosArgs[2].pa = fLogRmPhys;
    libosArgs[2].size = logBufSize;
    libosArgs[2].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
    libosArgs[2].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

    // [3] RMARGS  (LE bytes: 52 4D 41 52 47 53 00 00)
    // RMARGS data in separate buffer to avoid corrupting SHM PTE array
    if (!fRmargsBuf) {
        fRmargsBuf = IOMallocAligned(GSP_QUEUE_PAGE_SIZE, GSP_QUEUE_PAGE_SIZE);
        if (!fRmargsBuf) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fRmargsBuf, GSP_QUEUE_PAGE_SIZE, kIODirectionInOut, kernel_task);
        if (md) { md->prepare(); fRmargsPhys = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!fRmargsPhys) { IOFreeAligned(fRmargsBuf, GSP_QUEUE_PAGE_SIZE); fRmargsBuf = nullptr; return kIOReturnNoMemory; }
    }
    memset(fRmargsBuf, 0, GSP_QUEUE_PAGE_SIZE);
    libosArgs[3].id8 = 0x0000534752414D52ULL;
    libosArgs[3].pa = fRmargsPhys;
    libosArgs[3].size = GSP_QUEUE_PAGE_SIZE;
    libosArgs[3].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
    libosArgs[3].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

    // Fill RMARGS area with MESSAGE_QUEUE_INIT_ARGUMENTS (NVIDIA format)
    // Layout: sharedMemPA(8) + pteCnt(4) + pad(4) + cmdqOff(8) + msgqOff(8) +
    //         hdrSz(8) + elMin(8) + elMax(8) + hdrAlign(4) + elAlign(4) = 64 bytes
    volatile uint64_t *rm64 = (volatile uint64_t*)fRmargsBuf;
    volatile uint32_t *rm32 = (volatile uint32_t*)fRmargsBuf;
    rm64[0] = fShmPhys;                     // sharedMemPhysAddr @0 = SYSMEM (NVIDIA)
    rm32[2] = pteEntries;                   // pageTableEntryCount @8 (uint32)
    // 4 bytes padding @12
    rm64[2] = fCmdqOff;                     // cmdQueueOffset @16
    rm64[3] = fMsgqOff;                     // statQueueOffset @24
    rm64[4] = sizeof(GspMsgQueuePrefix);    // queueElementHdrSize @32 = 16
    rm64[5] = GSP_QUEUE_MSG_SIZE;           // queueElementSizeMin @40 = 4096
    rm64[6] = GSP_QUEUE_MSG_SIZE * 16;      // queueElementSizeMax @48 = 65536
    rm32[14] = 4;                            // queueHeaderAlign @56
    rm32[15] = 12;                           // queueElementAlign @60 (RM_PAGE_SHIFT)
    // Rest (srInitArguments, gpuInstance) already zero from memset
    __sync_synchronize();
    
    // RMARGS hex dump — compare with NVIDIA byte-a-byte
    {
        uint8_t *r = (uint8_t*)fRmargsBuf;
        IOLog("GA104: RMARGS hex (128 bytes):\n");
        for (int i = 0; i < 128; i += 16)
            IOLog("  [%3d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                  i, r[i], r[i+1], r[i+2], r[i+3], r[i+4], r[i+5], r[i+6], r[i+7],
                  r[i+8], r[i+9], r[i+10], r[i+11], r[i+12], r[i+13], r[i+14], r[i+15]);
    }
    
    // Debug: RMARGS values as ioreg properties
    uint32_t lo_shm = (uint32_t)(rm64[0] & 0xFFFFFFFF);
    uint32_t hi_shm = (uint32_t)(rm64[0] >> 32);
    setProperty("GA104_RMARGS_shmPA_lo", lo_shm, 32);
    setProperty("GA104_RMARGS_shmPA_hi", hi_shm, 32);
    setProperty("GA104_RMARGS_pteCnt", rm32[2], 32);
    setProperty("GA104_RMARGS_cmdqOff", (uint32_t)(rm64[2] & 0xFFFFFFFF), 32);
    setProperty("GA104_RMARGS_msgqOff", (uint32_t)(rm64[3] & 0xFFFFFFFF), 32);
    setProperty("GA104_RMARGS_hdrSz", (uint32_t)(rm64[4] & 0xFFFFFFFF), 32);
    setProperty("GA104_RMARGS_elSz", (uint32_t)(rm64[5] & 0xFFFFFFFF), 32);
    setProperty("GA104_RMARGS_elMax", (uint32_t)(rm64[6] & 0xFFFFFFFF), 32);

    setProperty("GA104P2_LibosPhys", fLibosPhys, 64);
    setProperty("GA104P2_PteCount", pteEntries, 32);

    // Save firmware image pointer for post-boot VRAM re-copy
    fFwImageData = fwImgData;
    fFwImageSize = fwImgSize;

    // Phase 2b: Copy critical buffers to WPR2 VRAM (firmware-accessible via page tables)
    // WPR2 queue base = heapAddr + queueOffset (within firmware's page table coverage)
    // Log buffers go after queues in VRAM
    if (fBar1Phys && fVramLayout.elfAddr && fVramLayout.queuePhysAddr) {
        uint64_t wpr2Base  = fVramLayout.queuePhysAddr;
        uint64_t wpr2Shm   = wpr2Base;
        uint64_t wpr2Libos = wpr2Base + shmSize;
        uint64_t wpr2Rmargs = wpr2Libos + GSP_QUEUE_PAGE_SIZE;
        uint64_t wpr2LogInit = wpr2Rmargs + GSP_QUEUE_PAGE_SIZE;
        uint64_t wpr2LogIntr = wpr2LogInit + logBufSize;
        uint64_t wpr2LogRm   = wpr2LogIntr + logBufSize;

        fVramCmdqEntryBase = fVRAMBase + wpr2Base + fCmdqOff + GSP_QUEUE_PAGE_SIZE;
        fVramMsgqEntryBase = fVRAMBase + wpr2Base + fMsgqOff + GSP_QUEUE_PAGE_SIZE;

        // sharedMemPhysAddr kept as SYSMEM (fShmPhys) — NVIDIA uses SYSMEM
        // Copy RMARGS to VRAM
        {
            IOMemoryDescriptor *mdRm = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2Rmargs, GSP_QUEUE_PAGE_SIZE, kIODirectionOut);
            if (mdRm) { mdRm->prepare(); mdRm->writeBytes(0, fRmargsBuf, GSP_QUEUE_PAGE_SIZE); mdRm->complete(); mdRm->release(); }
            uint64_t vr_shm = 0, vr_cmdq = 0;
            IOMemoryDescriptor *mdR = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2Rmargs, 48, kIODirectionIn);
            if (mdR) { mdR->prepare(); mdR->readBytes(0, &vr_shm, 8); 
                       mdR->readBytes(12, &vr_cmdq, 8); mdR->complete(); mdR->release(); }
            setProperty("GA104_VRAM_RMARGS_shmPA", vr_shm, 64);
            setProperty("GA104_VRAM_RMARGS_cmdqOff", vr_cmdq, 64);
        }

        // Copy SHM (page table + cmdq + msgq) to VRAM — firmware reads from here!
        {
            IOMemoryDescriptor *mdShm = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2Shm, shmSize, kIODirectionOut);
            if (mdShm) { mdShm->prepare(); mdShm->writeBytes(0, fShmBuf, shmSize); mdShm->complete(); mdShm->release(); }
            __sync_synchronize();
            IOLog("GA104: SHM copied to VRAM@0x%llx (size 0x%x)\n", wpr2Shm, shmSize);
        }

        // Copy log buffers to WPR2 (optional, firmware log mechanism)
        if (fLogInitBuf) {
            IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2LogInit, logBufSize, kIODirectionOut);
            if (md) { md->prepare(); md->writeBytes(0, fLogInitBuf, logBufSize); md->complete(); md->release(); }
        }
        if (fLogIntrBuf) {
            IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2LogIntr, logBufSize, kIODirectionOut);
            if (md) { md->prepare(); md->writeBytes(0, fLogIntrBuf, logBufSize); md->complete(); md->release(); }
        }
        if (fLogRmBuf) {
            IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + wpr2LogRm, logBufSize, kIODirectionOut);
            if (md) { md->prepare(); md->writeBytes(0, fLogRmBuf, logBufSize); md->complete(); md->release(); }
        }

        // Update LibOS pa to WPR2 offsets and copy
        libosArgs[0].pa = wpr2LogInit;
        libosArgs[1].pa = wpr2LogIntr;
        libosArgs[2].pa = wpr2LogRm;
        libosArgs[3].pa = wpr2Rmargs;
        IOMemoryDescriptor *mdLo = IOMemoryDescriptor::withPhysicalAddress(
            fBar1Phys + wpr2Libos, GSP_QUEUE_PAGE_SIZE, kIODirectionOut);
        if (mdLo) { mdLo->prepare(); mdLo->writeBytes(0, fLibosBuf, GSP_QUEUE_PAGE_SIZE); mdLo->complete(); mdLo->release(); }

        // Keep fLibosPhys = sysmem addr (firmware reads LibOS from sysmem via DMA)
        // NOT overwritten to VRAM — booter/GSP- RM expects sysmem address

        __sync_synchronize();
        IOLog("GA104: Buffers copied to WPR2: SHM@0x%llx LibOS@0x%llx RMARGS@0x%llx LOGS@0x%llx\n",
              wpr2Shm, wpr2Libos, wpr2Rmargs, wpr2LogInit);
    }

    IOLog("GA104: gspSetupQueues complete (4 LibOS entries + VRAM copy)\n");
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
    /* nwprHeapSize unused */ uint64_t nwprHeapSize_dummy = 0;

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



IOReturn GA104Device::patchFirmwareForBooter(void)
{
    if (!fGSPFirmware || !fGSPFirmware->isLoaded()) {
        IOLog("GA104: patchFirmwareForBooter: firmware not loaded\n");
        return kIOReturnNotReady;
    }

    uint32_t fwImgSize = 0;
    uint8_t *fwImgData = fGSPFirmware->getFWImage(&fwImgSize);
    if (!fwImgData || fwImgSize == 0) {
        IOLog("GA104: patchFirmwareForBooter: no fw image\n");
        return kIOReturnNotReady;
    }

    IOLog("GA104: Patching .fwimage for Booter (sz=%u)\n", fwImgSize);

    if (!fFWBuffer || fFWBufferSize < fwImgSize) {
        if (fFWBuffer) { IOFreeAligned(fFWBuffer, fFWBufferSize); fFWBuffer = nullptr; }
        fFWBuffer = IOMallocAligned(fwImgSize, PAGE_SIZE);
        if (!fFWBuffer) return kIOReturnNoMemory;
        IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
            (mach_vm_address_t)fFWBuffer, fwImgSize, kIODirectionInOut, kernel_task);
        uint64_t pa = 0;
        if (md) { md->prepare(); pa = md->getPhysicalSegment(0, nullptr); md->complete(); md->release(); }
        if (!pa) { IOFreeAligned(fFWBuffer, fwImgSize); fFWBuffer = nullptr; return kIOReturnNoMemory; }
        fFWBufferPhys = pa;
        fFWBufferSize = fwImgSize;
    }
    memcpy(fFWBuffer, fwImgData, fwImgSize);

    // Rebuild Radix3 page table for the new .fwimage size
    buildRadix3PageTable(fFWBufferSize);

    uint8_t *fw = (uint8_t*)fFWBuffer;
    uint32_t sz = fFWBufferSize;

    applyFirmwarePatches(fw, sz, true);

    __sync_synchronize();
    IOLog("GA104: Firmware patched for Booter OK\n");
    return kIOReturnSuccess;
}


void GA104Device::applyFirmwarePatches(uint8_t *fw, uint32_t sz, bool fullCounters)
{
    // Patches removed — they corrupted the firmware's init and dispatch loop
    // (writePtr 0x10, rxHdrOff 0x20, RxHeader single field — firmware finds queues correctly now)
    return;
    // Boot-mode flag at .fwimage+0x6c080: 0=normal_boot
    const uint32_t bootFlagOff = 0x6c080;
    if (sz > bootFlagOff) {
        fw[bootFlagOff] = 0x00;
        setProperty("GA104P2_BootFlag", (uint64_t)0, 32);
    }

    // Patch WPR2 reader functions to return valid values matching VRAM layout
    static const uint8_t gPatchWpr2Lo[8] = {0x37,0x05,0xF0,0x0B,0x67,0x80,0x00,0x00};
    static const uint8_t gPatchWpr2Hi[8] = {0x37,0x05,0x00,0x80,0x67,0x80,0x00,0x00};
    const uint32_t wpr2LoOff = 0x234;
    const uint32_t wpr2HiOff = 0x34a;
    const uint32_t copyStep = 0x2F000;
    const uint32_t numCopies = 6;
    for (uint32_t i = 0; i < numCopies; i++) {
        uint32_t lo = wpr2LoOff + i * copyStep;
        uint32_t hi = wpr2HiOff + i * copyStep;
        if (lo + sizeof(gPatchWpr2Lo) <= sz) {
            memcpy(fw + lo, gPatchWpr2Lo, sizeof(gPatchWpr2Lo));
            memcpy(fw + hi, gPatchWpr2Hi, sizeof(gPatchWpr2Hi));
            IOLog("GA104: WPR2 patch copy %u at 0x%x/0x%x\n", i, lo, hi);
        }
    }

    // Polling patch: replace c.beqz @ 0xB24E2 with JAL to poll code @ 0x7B960
    {
        const uint32_t kBeqzOff = 0xB24E2;
        const uint32_t kGapOff  = 0x7B960;
        static const uint8_t kBeqzJal[4] = {0x6f,0x40,0xfe,0xa3};
        static const uint8_t kPollCode[42] = {
            0xde,0x88,0x37,0x1e,0x11,0x00,0x83,0x22,
            0x0e,0xc0,0xb7,0x13,0xc0,0x00,0x03,0xa3,
            0x43,0x03,0x63,0x94,0x62,0x00,0x6f,0xb0,
            0x81,0x5b,0x6f,0xb0,0x61,0x5b,0x03,0xae,
            0x03,0x04,0x13,0x0e,0x1e,0x00,0x23,0xa0,
            0xc3,0x05
        };
        if (sz > kGapOff + sizeof(kPollCode) && sz > kBeqzOff + 4) {
            memcpy(fw + kGapOff, kPollCode, sizeof(kPollCode));
            memcpy(fw + kBeqzOff, kBeqzJal, 4);
            IOLog("GA104: Cmdq polling patch at 0x%x -> 0x%x\n", kBeqzOff, kGapOff);
            setProperty("GA104P2_CmdqPollPatched", (uint64_t)1, 8);
        }
    }

    // Patch ECALLs + skip pre-dispatch init to reach the main loop
    {
        static const uint8_t kEcallPatch[4] = {0x13,0x05,0x00,0x00};
        uint32_t ecallCount = 0;
        for (uint32_t ea = 0x60000; ea < sz - 4; ea += 4) {
            uint32_t insn;
            memcpy(&insn, fw + ea, 4);
            if (insn == 0x00000073) {
                memcpy(fw + ea, kEcallPatch, 4);
                ecallCount++;
            }
        }
        IOLog("GA104: Patched %u ECALLs\n", ecallCount);
        setProperty("GA104P2_EcallCount", (uint64_t)ecallCount, 32);
    }

    // Diagnostic counters
    {
        static const uint8_t kJ1F[4]={0x6f,0x40,0x3e,0xcc};
        static const uint8_t kC1Cd[16]={0x37,0x0e,0xc0,0x00,0x83,0x22,0x0e,0x05,0x93,0x82,0x12,0x00,0x23,0x28,0x5e,0x04};
        static const uint8_t kJ1B[4]={0x6f,0xb0,0x81,0x33};
        static const uint8_t kJ2F[4]={0x6f,0x40,0x5e,0xcc};
        static const uint8_t kC2Cd[16]={0x37,0x0e,0xc0,0x00,0x83,0x22,0x4e,0x05,0x93,0x82,0x12,0x00,0x23,0x2a,0x5e,0x04};
        static const uint8_t kJ2B[4]={0x6f,0xb0,0x61,0x33};
        static const uint8_t kJ3F[4]={0x6f,0x40,0xfe,0xcc};
        static const uint8_t kC3Cd[16]={0x37,0x0e,0xc0,0x00,0x83,0x22,0x8e,0x05,0x93,0x82,0x12,0x00,0x23,0x2c,0x5e,0x04};
        static const uint8_t kJ3B[4]={0x6f,0xb0,0xc1,0x32};
        static const uint8_t kJ4F[4]={0x6f,0x40,0x7e,0xcb};
        static const uint8_t kC4Cd[16]={0x37,0x0e,0xc0,0x00,0x83,0x22,0xce,0x05,0x93,0x82,0x12,0x00,0x23,0x2e,0x5e,0x04};
        static const uint8_t kJ4B[4]={0x6f,0xb0,0x21,0x34};
        static const uint8_t kJ5F[4]={0x6f,0x40,0x7e,0xcb};
        static const uint8_t kC5Cd[16]={0x37,0x0e,0xc0,0x00,0x83,0x22,0x0e,0x06,0x93,0x82,0x12,0x00,0x23,0x20,0x5e,0x06};
        static const uint8_t kJ5B[4]={0x6f,0xb0,0x41,0x34};
        auto mc = [&](uint32_t o, const uint8_t *d, uint32_t s) { if (o + s <= sz) memcpy(fw + o, d, s); };
        if (fullCounters) {
            static const uint8_t kC6Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x8e,0x06,0x93,0x82,0x12,0x00,0x23,0x26,0x5e,0x06,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC7Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x0e,0x07,0x93,0x82,0x12,0x00,0x23,0x2a,0x5e,0x06,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC8Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x8e,0x07,0x93,0x82,0x12,0x00,0x23,0x2e,0x5e,0x06,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC9Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x0e,0x08,0x93,0x82,0x12,0x00,0x23,0x22,0x5e,0x08,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC10Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x8e,0x08,0x93,0x82,0x12,0x00,0x23,0x26,0x5e,0x08,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC11Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x0e,0x09,0x93,0x82,0x12,0x00,0x23,0x2a,0x5e,0x08,0x6f,0xf0,0x5f,0xff};
            static const uint8_t kC12Cd[24]={0x6f,0x00,0xc0,0x00,0x37,0x0e,0xc0,0x00,0x83,0x22,0x8e,0x09,0x93,0x82,0x12,0x00,0x23,0x2e,0x5e,0x08,0x6f,0xf0,0x5f,0xff};
            uint32_t gp = 0x7B98A, ads[12] = {0xB2006,0xB201A,0xB201E,0xB2064,0xB207E,0xB2100,0xB2200,0xB2300,0xB2400,0xB2480,0xB24A0,0xB24C0};
            const uint8_t *fs[12]={kJ1F,kJ2F,kJ3F,kJ4F,kJ5F,kC6Cd,kC7Cd,kC8Cd,kC9Cd,kC10Cd,kC11Cd,kC12Cd};
            const uint8_t *cs[12]={kC1Cd,kC2Cd,kC3Cd,kC4Cd,kC5Cd,kC6Cd,kC7Cd,kC8Cd,kC9Cd,kC10Cd,kC11Cd,kC12Cd};
            const uint8_t *bs[5]={kJ1B,kJ2B,kJ3B,kJ4B,kJ5B};
            for (int ci = 0; ci < 5; ci++)
                mc(ads[ci], fs[ci], 4), mc(gp+ci*24, cs[ci], 16), mc(gp+ci*24+16, bs[ci], 4);
            for (int ci = 5; ci < 12; ci++)
                mc(ads[ci], fs[ci], 24);
            IOLog("GA104: Init counters @ 0xB2006,0xB2100,0xB2200,0xB2300,0xB2400,0xB2480,0xB24A0,0xB24C0\n");
        } else {
            uint32_t gp = 0x7B98A, ads[5] = {0xB2006,0xB201A,0xB201E,0xB2064,0xB207E};
            const uint8_t *fs[5]={kJ1F,kJ2F,kJ3F,kJ4F,kJ5F};
            const uint8_t *cs[5]={kC1Cd,kC2Cd,kC3Cd,kC4Cd,kC5Cd};
            const uint8_t *bs[5]={kJ1B,kJ2B,kJ3B,kJ4B,kJ5B};
            for (int ci = 0; ci < 5; ci++)
                mc(ads[ci], fs[ci], 4), mc(gp+ci*24, cs[ci], 16), mc(gp+ci*24+16, bs[ci], 4);
            IOLog("GA104: Init counters @ 0xB2006,0xB201A,0xB201E,0xB2064,0xB207E\n");
        }
    }

    // Write page table descriptors at fw+0x24200 (kernel reads from here)
    {
        struct PtdDesc { uint64_t va, pa, size, attr; uint8_t aperture, pad[7]; };
        const uint32_t descTableOff = 0x24200;
        const uint32_t descCountOff = 0x24480;
        uint32_t descIdx = 0;
        PtdDesc descs[16];
        bzero(descs, sizeof(descs));
        auto addDesc = [&](uint64_t va, uint64_t pa, uint64_t sz_, uint64_t attr, int apt) {
            if (descIdx < 16 && sz > descTableOff + (descIdx + 1) * 40) {
                descs[descIdx].va = va; descs[descIdx].pa = pa;
                descs[descIdx].size = sz_; descs[descIdx].attr = attr;
                descs[descIdx].aperture = apt;
                for (int b = 0; b < 32; b++)
                    if (sz_ & (1ULL << b)) { descs[descIdx].size = 1ULL << b; break; }
                memcpy(fw + descTableOff + descIdx * 40, descs + descIdx, 40);
                descIdx++;
            }
        };
        // fVramCtx+0x3000: page table descriptor array
        addDesc(0x00000000, 0x00000000, fVRAMSize, 0x80000006, 3); // FB aperture
        if (sz > descCountOff + 4) {
            uint32_t nd = descIdx;
            memcpy(fw + descCountOff, &nd, 4);
        }
        setProperty("GA104P2_DescCount", (uint64_t)descIdx, 32);
    }
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

    // SEC2 signature (embedded)
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

