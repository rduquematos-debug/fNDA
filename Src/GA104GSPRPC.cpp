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

IOReturn GA104Device::sendGspRpcAllocRoot()
{
    if (!fGSPProtocol || !fMsgqTx || !fMsgqEntryBase) {
        IOLog("GA104: sendGspRpcAllocRoot: not ready\n");
        return kIOReturnNotReady;
    }

    // First check if ALLOC_ROOT response is already in msgq
    uint32_t wPtr = fMsgqTx->writePtr;
    for (uint32_t ri = 0; ri < wPtr; ri++) {
        uint32_t eIdx = ri % GSP_QUEUE_MSG_COUNT;
        uint8_t *mEntry = fMsgqEntryBase + eIdx * GSP_QUEUE_MSG_SIZE;
        GspMsgQueuePrefix *mPre = (GspMsgQueuePrefix*)mEntry;
        GspRpcMessageHeader *mRpc = (GspRpcMessageHeader*)(mEntry + sizeof(GspMsgQueuePrefix));
        if (mRpc->function == NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC) {
            IOLog("GA104: ALLOC_ROOT response found in msgq: rpcResult=0x%x\n", mRpc->rpcResult);
            if (mRpc->rpcResult == 0) {
                GspRmAllocParams *rmOut = (GspRmAllocParams*)((uint8_t*)mRpc + sizeof(GspRpcMessageHeader));
                setProperty("GA104GSP_RootObj", (uint64_t)rmOut->hObject, 32);
            } else {
                setProperty("GA104GSP_RootFail", (uint64_t)mRpc->rpcResult, 32);
            }
            return kIOReturnSuccess;
        }
    }

    // Configure IRQ routing before sending (doorbell must reach RISC-V core)
    writeReg32(FALCON_IRQDEST, 0x00);  // 0=RISC-V, 1=HOST — route ALL to RISC-V
    writeAbsReg32(NV_PRISCV_RISCV_IRQDEST, 0x00); // RISC-V IRQDEST: route to core
    writeReg32(FALCON_IRQSCLR, 0xFFFFFFFF);
    __sync_synchronize();

    // Try ALLOC_ROOT via cmdq
    IOLog("GA104: Sending ALLOC_ROOT via cmdq...\n");
    GspRpcMessageHeader arMsg, arReply;
    uint32_t arReplySz = 0;
    bzero(&arMsg, sizeof(arMsg));
    bzero(&arReply, sizeof(arReply));
    fGSPProtocol->buildRmAlloc(&arMsg, 0, 0, 0, NV01_ROOT, 0, nullptr);
    uint32_t arPayloadSize = arMsg.length - sizeof(GspRpcMessageHeader);
    IOReturn rpcRet = sendGspRpc(&arMsg,
        (uint8_t*)&arMsg + sizeof(GspRpcMessageHeader),
        arPayloadSize, &arReply, sizeof(arReply), &arReplySz, 10000);
    if (rpcRet == kIOReturnSuccess) {
        IOLog("GA104: Cmdq ALLOC_ROOT rpcResult=0x%x\n", arReply.rpcResult);
        if (arReply.rpcResult == 0) {
            GspRmAllocParams *rmOut = (GspRmAllocParams*)((uint8_t*)&arReply + sizeof(GspRpcMessageHeader));
            setProperty("GA104GSP_RootObj", (uint64_t)rmOut->hObject, 32);
            return kIOReturnSuccess;
        }
    }

    IOLog("GA104: ALLOC_ROOT via cmdq failed (0x%x)\n", rpcRet);
    return rpcRet;
}

#pragma mark - Display Alloc Chain

IOReturn GA104Device::sendGspRpcAllocDevice()
{
    if (!fGSPProtocol) return kIOReturnNotReady;
    IOLog("GA104: Allocating GSP device...\n");

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    NvHandle hClient = fRmRoot ? fRmRoot : 0;
    NvHandle hDevice = NVKM_RM_DEVICE;
    fGSPProtocol->buildAllocDevice(&msg, hClient, hDevice, fDeviceID);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *rm = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmDevice = rm->hObject;
        setProperty("GA104GSP_DeviceObj", (uint64_t)rm->hObject, 32);
        IOLog("GA104: Device allocated (hObject=0x%x)\n", rm->hObject);
        return kIOReturnSuccess;
    }
    IOLog("GA104: Device alloc failed (ret=0x%x result=0x%x)\n", ret, reply.rpcResult);
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcAllocSubdevice()
{
    if (!fGSPProtocol || !fRmDevice) return kIOReturnNotReady;
    IOLog("GA104: Allocating GSP subdevice...\n");

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    NvHandle hClient = fRmRoot ? fRmRoot : 0;
    NvHandle hSubdevice = NVKM_RM_SUBDEVICE;
    fGSPProtocol->buildAllocSubdevice(&msg, hClient, fRmDevice, hSubdevice);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *rm = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmSubdevice = rm->hObject;
        setProperty("GA104GSP_SubdeviceObj", (uint64_t)rm->hObject, 32);
        IOLog("GA104: Subdevice allocated (hObject=0x%x)\n", rm->hObject);
        return kIOReturnSuccess;
    }
    IOLog("GA104: Subdevice alloc failed (ret=0x%x result=0x%x)\n", ret, reply.rpcResult);
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcAllocDisp()
{
    if (!fGSPProtocol || !fRmSubdevice) return kIOReturnNotReady;
    IOLog("GA104: Allocating GSP display...\n");

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    NvHandle hClient = fRmRoot ? fRmRoot : 0;
    NvHandle hDisp = NVKM_RM_DISP;
    fGSPProtocol->buildAllocDisp(&msg, hClient, fRmSubdevice, hDisp, 0xF, 0xF);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *rm = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmDisp = rm->hObject;
        setProperty("GA104GSP_DispObj", (uint64_t)rm->hObject, 32);
        IOLog("GA104: Display allocated (hObject=0x%x)\n", rm->hObject);
        return kIOReturnSuccess;
    }
    IOLog("GA104: Display alloc failed (ret=0x%x result=0x%x)\n", ret, reply.rpcResult);
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcDisplayInit()
{
    // Full display initialization chain: Device → Subdevice → Disp
    IOReturn ret;
    ret = sendGspRpcAllocDevice();
    if (ret != kIOReturnSuccess) return ret;
    ret = sendGspRpcAllocSubdevice();
    if (ret != kIOReturnSuccess) return ret;
    ret = sendGspRpcAllocDisp();
    if (ret != kIOReturnSuccess) return ret;
    IOLog("GA104: Display init chain complete (Device=0x%x Subdev=0x%x Disp=0x%x)\n",
          (uint32_t)fRmDevice, (uint32_t)fRmSubdevice, (uint32_t)fRmDisp);
    setProperty("GA104GSP_DisplayReady", true);
    return kIOReturnSuccess;
}

#pragma mark - Display Control RPCs

IOReturn GA104Device::sendGspRpcDfpGetAttachedIds(uint32_t *displayIds, uint32_t *count)
{
    if (!fGSPProtocol || !fRmSubdevice || !displayIds || !count)
        return kIOReturnBadArgument;

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    fGSPProtocol->buildDfpGetAttachedIds(&msg, fRmSubdevice);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        uint32_t *ids = (uint32_t*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        uint32_t numIds = (replySz - sizeof(GspRpcMessageHeader)) / sizeof(uint32_t);
        if (numIds > *count) numIds = *count;
        for (uint32_t i = 0; i < numIds; i++) displayIds[i] = ids[i];
        *count = numIds;
        IOLog("GA104: Attached displays: %u\n", numIds);
        return kIOReturnSuccess;
    }
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcDfpGetInfo(uint32_t displayId)
{
    if (!fGSPProtocol || !fRmSubdevice) return kIOReturnNotReady;

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    fGSPProtocol->buildDfpGetInfo(&msg, fRmSubdevice, displayId);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        uint32_t dataSize = replySz - sizeof(GspRpcMessageHeader);
        if (dataSize > 0 && dataSize <= sizeof(fEDID)) {
            memcpy(fEDID, (uint8_t*)&reply + sizeof(GspRpcMessageHeader), dataSize);
            fEDIDSize = dataSize;
            setProperty("GA104_EDIDSize", fEDIDSize, 32);
            IOLog("GA104: EDID for display 0x%x: %u bytes\n", displayId, fEDIDSize);
        }
        return kIOReturnSuccess;
    }
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcOrAssign(uint32_t displayId, uint32_t sorIndex, uint32_t protocol)
{
    if (!fGSPProtocol || !fRmSubdevice) return kIOReturnNotReady;

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    fGSPProtocol->buildOrAssign(&msg, fRmSubdevice, displayId, ~(1 << sorIndex), protocol);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        IOLog("GA104: Display 0x%x assigned to SOR %u protocol %u\n", displayId, sorIndex, protocol);
        return kIOReturnSuccess;
    }
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcHeadSetTimings(uint32_t head, uint32_t width,
                                                uint32_t height, uint32_t refreshHz)
{
    if (!fGSPProtocol || !fRmDisp) return kIOReturnNotReady;

    GSPModesetParams params;
    bzero(&params, sizeof(params));
    params.headIndex = head;
    params.sorIndex = head;
    params.width = width;
    params.height = height;
    params.refreshHz = refreshHz;
    params.bpp = 32;
    params.pitch = width * 4;
    params.framebufferAddr = fFB.fbAddr;

    // Timings for 1920x1080@60
    params.hTotal = TIMING_1920x1080_60_HTOTAL;
    params.vTotal = TIMING_1920x1080_60_VTOTAL;
    params.hSyncStart = TIMING_1920x1080_60_HSYNC_START;
    params.hSyncEnd = TIMING_1920x1080_60_HSYNC_END;
    params.vSyncStart = TIMING_1920x1080_60_VSYNC_START;
    params.vSyncEnd = TIMING_1920x1080_60_VSYNC_END;
    params.hBlankStart = TIMING_1920x1080_60_HBLANK_START;
    params.hBlankEnd = TIMING_1920x1080_60_HBLANK_END;
    params.vBlankStart = TIMING_1920x1080_60_VBLANK_START;
    params.vBlankEnd = TIMING_1920x1080_60_VBLANK_END;
    params.clockKHz = TIMING_1920x1080_60_PCLOCK_KHZ;
    params.colorFormat = NV_PWINDOW_FORMAT_B8G8R8A8;

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    fGSPProtocol->buildHeadSetTimings(&msg, fRmSubdevice, head, &params);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        IOLog("GA104: Head %u timings set: %ux%u@%u\n", head, width, height, refreshHz);
        return kIOReturnSuccess;
    }
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

IOReturn GA104Device::sendGspRpcFlip(uint32_t head)
{
    if (!fGSPProtocol || !fRmSubdevice) return kIOReturnNotReady;

    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    fGSPProtocol->buildFlip(&msg, fRmSubdevice, head, fFB.fbAddr, fFB.pitch);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader),
        payloadSz, &reply, sizeof(reply), &replySz, 10000);

    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        IOLog("GA104: Flip head %u to FB 0x%llx\n", head, fFB.fbAddr);
        return kIOReturnSuccess;
    }
    return ret == kIOReturnSuccess ? kIOReturnError : ret;
}

static void nvgMsgqCalcOffsets(uint32_t hdrAlign, uint32_t entryAlign,
                                uint32_t *rxHdrOff, uint32_t *entryOff);
static void nvgMsgqTxCreate(GspMsgqTxHeader *pTxHdr, uint32_t size, uint32_t msgSize,
                             uint32_t hdrAlign, uint32_t entryAlign, uint32_t flags);
static int nvgMsgqRxLink(const GspMsgqTxHeader *pRemoteTxHdr, uint32_t size, uint32_t msgSize);
static uint32_t nvgMsgqRxAvailable(uint32_t rxReadPtr, uint32_t remoteWritePtr, uint32_t msgCount);
static void nvgMsgqTxSubmit(GspMsgqTxHeader *pTxHdr, uint32_t n);

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
static void nvgMsgqCalcOffsets(uint32_t hdrAlign, uint32_t entryAlign,
                                uint32_t *rxHdrOff, uint32_t *entryOff)
{
    *rxHdrOff = (sizeof(GspMsgqTxHeader) + (1 << hdrAlign) - 1) & ~((1 << hdrAlign) - 1);
    uint32_t rxEnd = *rxHdrOff + sizeof(GspMsgqRxHeader);
    *entryOff = (rxEnd + (1 << entryAlign) - 1) & ~((1 << entryAlign) - 1);
}
static void nvgMsgqTxCreate(GspMsgqTxHeader *pTxHdr, uint32_t size, uint32_t msgSize,
                             uint32_t hdrAlign, uint32_t entryAlign, uint32_t flags)
{
    uint32_t rxHdrOff, entryOff;
    nvgMsgqCalcOffsets(hdrAlign, entryAlign, &rxHdrOff, &entryOff);

    pTxHdr->version  = NVG_MSGQ_VERSION;
    pTxHdr->size     = size;
    pTxHdr->msgSize  = msgSize;
    pTxHdr->msgCount = (size - entryOff) / msgSize;
    pTxHdr->writePtr = 0;
    pTxHdr->flags    = flags;
    pTxHdr->rxHdrOff = rxHdrOff;
    pTxHdr->entryOff = entryOff;
    __sync_synchronize();
}
static int nvgMsgqRxLink(const GspMsgqTxHeader *pRemoteTxHdr, uint32_t size, uint32_t msgSize)
{
    GspMsgqTxHeader rx;
    memcpy(&rx, (const void*)pRemoteTxHdr, sizeof(rx));
    __sync_synchronize();

    // Validate — matching NVIDIA msgqRxLink checks
    if (rx.version != NVG_MSGQ_VERSION) return -9;
    if (rx.size != size) return -7;
    if (rx.msgSize != msgSize) return -8;
    if (rx.msgSize < NVG_MSGQ_MSG_SIZE_MIN) return -2;
    if (msgSize > size) return -3;
    if (rx.rxHdrOff < sizeof(GspMsgqTxHeader)) return -10;
    if (rx.entryOff < rx.rxHdrOff + sizeof(GspMsgqRxHeader)) return -10;

    uint32_t expectedCount = (size - rx.entryOff) / msgSize;
    if (rx.msgCount != expectedCount) return -10;

    // Write readPtr=0 (signal RX ready) — at their readPtr location (swapped)
    // In swapped mode, our readPtr is at our rxHdr location
    // But we're just linking — firmware needs to know we're ready
    // The simplest signal: the firmware checks if rxReadPtr has been written
    // For now, just validate the header exists
    
    return 0; // Linked successfully
}
static uint32_t nvgMsgqRxAvailable(uint32_t rxReadPtr, uint32_t remoteWritePtr, uint32_t msgCount)
{
    if (remoteWritePtr >= msgCount) return 0;
    uint32_t avail = remoteWritePtr + msgCount - rxReadPtr;
    if (avail >= msgCount) avail -= msgCount;
    return avail;
}
static void nvgMsgqTxSubmit(GspMsgqTxHeader *pTxHdr, uint32_t n)
{
    uint32_t wp = pTxHdr->writePtr + n;
    if (wp >= pTxHdr->msgCount) wp -= pTxHdr->msgCount;
    pTxHdr->writePtr = wp;
    __sync_synchronize();
}
IOReturn GA104Device::sendGspRpc(GspRpcMessageHeader *msg, void *payload,
                                  uint32_t payloadSize,
                                  GspRpcMessageHeader *reply, uint32_t replyMaxSize,
                                  uint32_t *replySize, uint32_t timeoutMs)
{
    if (!msg || !reply || !fCmdqTx || !fMsgqTx) return kIOReturnNotReady;

    uint32_t wp = fCmdqTx->writePtr;
    uint32_t idx = wp % GSP_QUEUE_MSG_COUNT;
    uint8_t *entry = fCmdqEntryBase + idx * GSP_QUEUE_MSG_SIZE;
    uint32_t totalLen = sizeof(GspMsgQueuePrefix) + sizeof(GspRpcMessageHeader) + payloadSize;
    if (totalLen > GSP_QUEUE_MSG_SIZE) return kIOReturnNoSpace;

    memset(entry, 0, GSP_QUEUE_MSG_SIZE);
    GspMsgQueuePrefix *pre = (GspMsgQueuePrefix*)entry;
    pre->mctpHeader = GSP_MCTP_HEADER_SINGLE; pre->nvdmHeader = GSP_NVDM_HEADER_RM_RPC;
    pre->seqNum = wp;
    GspRpcMessageHeader *rpcDst = (GspRpcMessageHeader*)(entry + sizeof(GspMsgQueuePrefix));
    memcpy(rpcDst, msg, sizeof(GspRpcMessageHeader));
    if (payload && payloadSize > 0)
        memcpy((uint8_t*)rpcDst + sizeof(GspRpcMessageHeader), payload, payloadSize);

    uint64_t cs = 0;
    for (uint32_t ci = 0; ci < GSP_QUEUE_MSG_SIZE / 8; ci++)
        cs ^= ((uint64_t*)entry)[ci];
    pre->checksum = (uint32_t)((cs >> 32) ^ (cs & 0xFFFFFFFF));
    __sync_synchronize();

    fCmdqTx->writePtr = wp + 1;
    __sync_synchronize();

    // Doorbell: usar aliased offset (não writeAbsReg32 — fora do BAR0!)
    uint32_t wpDoor = fCmdqTx ? fCmdqTx->writePtr : 0;
    writeReg32(GSP_DOORBELL_REL, 0);  // aliased 0x0C00 -> QUEUE_HEAD(0)
    __sync_synchronize();
    setProperty("GA104_DoorbellMailbox", (uint32_t)fCmdqTx->writePtr, 32);

    // Poll msgq (system memory — firmware writes via coherent DMA)
    uint32_t rp = fLastMsgqRp;
    uint32_t targetSeq = wp;
    uint32_t targetFunc = msg->function;
    uint32_t pollMs = 0;
    while (pollMs < timeoutMs) {
        uint32_t wPtr = fMsgqTx->writePtr;
        while (rp < wPtr) {
            uint32_t eIdx = rp % GSP_QUEUE_MSG_COUNT;
            uint8_t *mEntry = fMsgqEntryBase + eIdx * GSP_QUEUE_MSG_SIZE;
            GspMsgQueuePrefix *mPre = (GspMsgQueuePrefix*)mEntry;
            GspRpcMessageHeader *mRpc = (GspRpcMessageHeader*)(mEntry + sizeof(GspMsgQueuePrefix));

            if (mPre->seqNum == targetSeq && mRpc->function == targetFunc) {
                uint32_t copySz = (mRpc->length < replyMaxSize) ? mRpc->length : replyMaxSize;
                memcpy(reply, mRpc, copySz);
                if (replySize) *replySize = copySz;
                fLastMsgqRp = rp + 1;
                uint32_t *msgqReadPtr = (uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + 0x20);
                *msgqReadPtr = fLastMsgqRp;
                __sync_synchronize();
                return kIOReturnSuccess;
            }
            rp++;
        }
        fLastMsgqRp = wPtr;
        IOSleep(1);
        pollMs++;
    }
    fLastMsgqRp = fMsgqTx->writePtr;

    // Dump system memory msgq header to check where firmware writes
    if (fMsgqTx) {
        IOLog("GA104: SYSMEM msgq: ver=%u sz=%u entryOff=0x%x writePtr=%u\n",
              fMsgqTx->version, fMsgqTx->size, fMsgqTx->entryOff,
              (uint32_t)fMsgqTx->writePtr);
        volatile uint32_t *sysEntry = (volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + GSP_QUEUE_PAGE_SIZE);
        uint32_t sysFirst = sysEntry[0];
        if (sysFirst) IOLog("GA104: SYSMEM msgq entry[0]+0 = 0x%08x\n", sysFirst);
        // Full dump of entry[0] in sysmem
        if (fMsgqTx->writePtr > 0) {
            volatile uint32_t *e32 = (volatile uint32_t*)((uint8_t*)fShmBuf + fMsgqOff + GSP_QUEUE_PAGE_SIZE);
            IOLog("GA104: msgq sysmem entry[0] raw:\n");
            for (int di = 0; di < 20; di++) {
                if (e32[di]) IOLog("  [0x%02x] 0x%08x\n", di*4, e32[di]);
            }
            GspMsgQueuePrefix *pre = (GspMsgQueuePrefix*)((uint8_t*)fShmBuf + fMsgqOff + GSP_QUEUE_PAGE_SIZE);
            GspRpcMessageHeader *rpc = (GspRpcMessageHeader*)((uint8_t*)pre + sizeof(GspMsgQueuePrefix));
            IOLog("  decoded: seq=%u chk=0x%08x func=0x%04x result=0x%08x sig=0x%08x\n",
                  pre->seqNum, pre->checksum,
                  rpc->function, rpc->rpcResult, rpc->signature);
        }
    }

    // Dump cmdq state to check if firmware processes commands
    if (fShmBuf && fCmdqTx) {
        uint32_t cmdqReadPtr = *(volatile uint32_t*)((uint8_t*)fShmBuf + fCmdqOff + 0x20);
        IOLog("GA104: cmdq: writePtr=%u readPtr=%u consumed=%u\n",
              (uint32_t)fCmdqTx->writePtr, cmdqReadPtr,
              (uint32_t)fCmdqTx->writePtr - cmdqReadPtr);
    }

    // Dump VRAM cmdq to verify polling code addresses
    if (fBar1Phys) {
        uint64_t vramWpOff = 0xC00000 + fCmdqOff + 0x10;
        IOMemoryDescriptor *mdR = IOMemoryDescriptor::withPhysicalAddress(fBar1Phys + vramWpOff, 8, kIODirectionIn);
        uint32_t vwPtr = 0, vrPtr = 0;
        if (mdR) {
            mdR->prepare();
            mdR->readBytes(0, &vwPtr, 4);
            mdR->readBytes(4, &vrPtr, 4);
            mdR->complete();
            mdR->release();
        }
        IOLog("GA104: VRAM cmdq: wPtr=%u rPtr=%u fCmdqOff=0x%llx fMsgqOff=0x%llx\n",
              vwPtr, vrPtr, fCmdqOff, fMsgqOff);

        // Also read from dynamically calculated address
        uint64_t vramWpAddr = (uint64_t)(uintptr_t)fVramCmdqEntryBase;
        if (vramWpAddr) {
            volatile uint32_t *vWp2 = (volatile uint32_t*)(vramWpAddr - GSP_QUEUE_PAGE_SIZE + 0x18);
            IOLog("GA104: VRAM cmdq via fVramCmdqEntryBase: wPtr=%u (addr=0x%llx)\n", *vWp2,
                  (uint64_t)((uintptr_t)fVramCmdqEntryBase - GSP_QUEUE_PAGE_SIZE + 0x18));
        }

        // Check if the poll code was copied to VRAM
        // The .fwimage is at elfAddr in VRAM. The gap 0x6E0E2 is within .fwimage
        // but at elfAddr offset:
        uint64_t pollVramOff = fVramLayout.elfAddr + 0x6E0E2;
        if (pollVramOff + 8 <= fVRAMSize && fBar1Phys) {
            IOMemoryDescriptor *mdP = IOMemoryDescriptor::withPhysicalAddress(
                fBar1Phys + pollVramOff, 8, kIODirectionIn);
            uint32_t pcFirst = 0, pcSecond = 0;
            if (mdP) {
                mdP->prepare(); mdP->readBytes(0, &pcFirst, 4); mdP->readBytes(4, &pcSecond, 4);
                mdP->complete(); mdP->release();
            }
            IOLog("GA104: VRAM pollCode[0]=0x%08x [1]=0x%08x (at VRAM+0x%llx)\n",
                  pcFirst, pcSecond, pollVramOff);
        }
    }

    // Dump comprehensive VRAM layout + LibOS parameters
    // NOTE: vramSHM/vramLibos are debug-only — firmware reads from actual queuePhysAddr
    if (fVRAMBase) {
        uint32_t shmSz = (uint32_t)fCmdqOff + GSP_QUEUE_SIZE * 2;
        uint64_t vramSHM    = fVramLayout.queuePhysAddr;  // REAL address, not hardcoded
        uint64_t vramLibos  = vramSHM + shmSz;
        uint64_t vramRmargs = vramLibos + GSP_QUEUE_PAGE_SIZE;
        uint64_t vramLogInit = vramRmargs + GSP_QUEUE_PAGE_SIZE;
        uint64_t vramLogIntr = vramLogInit + 0x10000;
        uint64_t vramLogRm   = vramLogIntr + 0x10000;

        IOLog("GA104: === VRAM Layout dump ===\n");
        IOLog("  fVramLayout: wpr2=[0x%llx +0x%llx) elf=[0x%llx +0x%llx) boot=[0x%llx +0x%llx)\n",
              fVramLayout.wpr2Addr, fVramLayout.wpr2Size, fVramLayout.elfAddr, fVramLayout.elfSize,
              fVramLayout.bootAddr, fVramLayout.bootSize);
        IOLog("  fVramLayout: heap=[0x%llx +0x%llx) frts=[0x%llx +0x%llx) fbSize=0x%llx\n",
              fVramLayout.heapAddr, fVramLayout.heapSize, fVramLayout.frtsAddr, fVramLayout.frtsSize,
              fVramLayout.fbSize);
        IOLog("  VRAM buffers: SHM@0x%llx LibOS@0x%llx RMARGS@0x%llx LOGINIT@0x%llx LOGRM@0x%llx\n",
              vramSHM, vramLibos, vramRmargs, vramLogInit, vramLogRm);

        // SHM cmdq header
        volatile uint32_t *cmdqHdr = (volatile uint32_t*)(fVRAMBase + vramSHM + fCmdqOff);
        IOLog("  SHM cmdq@0x%llx: ver=%u size=%u entryOff=%u msgSize=%u msgCount=%u wPtr=%u flags=%u rxOff=0x%x\n",
              vramSHM + fCmdqOff, cmdqHdr[0], cmdqHdr[1], cmdqHdr[2], cmdqHdr[3],
              cmdqHdr[4], cmdqHdr[5], cmdqHdr[6], cmdqHdr[7]);

        // SHM msgq header
        volatile uint32_t *msgqHdr = (volatile uint32_t*)(fVRAMBase + vramSHM + fMsgqOff);
        IOLog("  SHM msgq@0x%llx: ver=%u size=%u entryOff=%u msgSize=%u msgCount=%u wPtr=%u flags=%u rxOff=0x%x\n",
              vramSHM + fMsgqOff, msgqHdr[0], msgqHdr[1], msgqHdr[2], msgqHdr[3],
              msgqHdr[4], msgqHdr[5], msgqHdr[6], msgqHdr[7]);

        // LibOS args in VRAM (each entry: id8, pa, size, kind, loc = 32 bytes)
        volatile uint32_t *libos = (volatile uint32_t*)(fVRAMBase + vramLibos);
        for (int i = 0; i < 4; i++) {
            volatile uint32_t *e = libos + i * 8; // 8 dwords per entry
            uint64_t id8 = (uint64_t)e[0] | ((uint64_t)e[1] << 32);
            uint64_t pa  = (uint64_t)e[2] | ((uint64_t)e[3] << 32);
            uint64_t sz  = (uint64_t)e[4] | ((uint64_t)e[5] << 32);
            uint32_t kind = e[6];
            uint32_t loc  = e[7];
            IOLog("  LibOS[%d]: id8=0x%016llx pa=0x%llx sz=%llu kind=%u loc=%u\n",
                  i, id8, pa, sz, kind, loc);
        }

        // RMARGS content (first 16 dwords)
        volatile uint32_t *rm = (volatile uint32_t*)(fVRAMBase + vramRmargs);
        IOLog("  RMARGS: shmPA=0x%08x%08x pteCnt=%u cmdqOff=0x%x msgqOff=0x%x\n",
              rm[1], rm[0], rm[2], rm[3], rm[5]);
        IOLog("  RMARGS: locklessCmdqOff=0x%x locklessStatqOff=0x%x sr.oldLevel=%u sr.flags=%u gpuInst=%u\n",
              rm[7], rm[9], rm[11], rm[12], rm[14]);

        // LOGINIT header
        volatile uint32_t *li = (volatile uint32_t*)(fVRAMBase + vramLogInit);
        uint32_t lver = li[0];
        uint64_t lput = *(volatile uint64_t*)(li + 2);
        uint64_t lptr = *(volatile uint64_t*)(li + 4);
        uint64_t lsz  = *(volatile uint64_t*)(li + 6);
        IOLog("  LOGINIT@0x%llx: ver=%u put=0x%llx ptr=0x%llx sz=0x%llx\n",
              vramLogInit, lver, lput, lptr, lsz);

        // LOGINIT data area (first 64 chars after header)
        volatile uint8_t *logData = (volatile uint8_t*)(fVRAMBase + vramLogInit + 0x100);
        char logStr[65];
        for (int di = 0; di < 64; di++) {
            uint8_t c = logData[di];
            logStr[di] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        logStr[64] = '\0';
        IOLog("  LOGINIT data: %s\n", logStr);

        // LOGRM header
        volatile uint32_t *lr = (volatile uint32_t*)(fVRAMBase + vramLogRm);
        uint32_t rver = lr[0];
        uint64_t rput = *(volatile uint64_t*)(lr + 2);
        uint64_t rptr = *(volatile uint64_t*)(lr + 4);
        uint64_t rsz  = *(volatile uint64_t*)(lr + 6);
        IOLog("  LOGRM@0x%llx: ver=%u put=0x%llx ptr=0x%llx sz=0x%llx\n",
              vramLogRm, rver, rput, rptr, rsz);

        // LOGRM data area
        volatile uint8_t *rmData = (volatile uint8_t*)(fVRAMBase + vramLogRm + 0x100);
        for (int di = 0; di < 64; di++) {
            uint8_t c = rmData[di];
            logStr[di] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        logStr[64] = '\0';
        IOLog("  LOGRM data: %s\n", logStr);

        // ELF magic at elfAddr
        volatile uint32_t *elfMagic = (volatile uint32_t*)(fVRAMBase + fVramLayout.elfAddr);
        IOLog("  ELF@0x%llx: magic=0x%08x\n", fVramLayout.elfAddr, *elfMagic);

        IOLog("GA104: === End VRAM dump ===\n");
    }

    // Dump page table entries from VRAM (verify they match expected physical addresses)
    if (fBar1Virt && fVramLayout.queuePhysAddr) {
        volatile uint64_t *ptVram = (volatile uint64_t*)(fBar1Virt + fVramLayout.queuePhysAddr);
        IOLog("GA104: Page table in VRAM @ 0x%llx:\n", fVramLayout.queuePhysAddr);
        for (int i = 0; i < 8; i++) {
            IOLog("  PT[%d] = 0x%016llx\n", i, ptVram[i]);
        }
        if (fShmBuf) {
            uint64_t *ptSys = (uint64_t*)fShmBuf;
            IOLog("GA104: Page table SYSMEM (first 4):\n");
            for (int i = 0; i < 4; i++)
                IOLog("  SYS_PT[%d] = 0x%016llx\n", i, ptSys[i]);
        }
    }

    // Dump Falcon exception registers
    IOLog("GA104: Falcon exception regs:\n");
    IOLog("  CPUCTL=0x%08x\n", readReg32(FALCON_CPUCTL));
    IOLog("  EXTERRADDR=0x%08x\n", readReg32(FALCON_EXTERRADDR));
    IOLog("  EXTERRSTAT=0x%08x\n", readReg32(FALCON_EXTERRSTAT));
    IOLog("  MAILBOX0=0x%08x\n", readReg32(FALCON_MAILBOX0));
    IOLog("  MAILBOX1=0x%08x\n", readReg32(FALCON_MAILBOX1));
    IOLog("  SCRATCH2=0x%08x\n", readReg32(0x080));
    // GSP interrupt registers
    IOLog("  IRQSTAT=0x%08x IRQMSET=0x%08x IRQMCLR=0x%08x RISCV_IRQMASK=0x%08x\n",
          readReg32(FALCON_IRQSTAT), readReg32(FALCON_IRQMSET),
          readReg32(FALCON_IRQMCLR), readReg32(NV_PGSP_RISCV_IRQMASK_REL));
    IOLog("  RISCV_STATUS=0x%08x RISCV_ITCMSK=0x%08x\n",
          readReg32(NV_PGSP_RISCV_STATUS), readReg32(NV_PGSP_RISCV_ITCMSK));

    // Dump work queue structure (what the firmware reads when it exits idle)
    if (fVRAMBase) {
        volatile uint64_t *wq = (volatile uint64_t*)(fVRAMBase + 0x10085000);
        IOLog("GA104: Work queue struct at 0x10085000:\n");
        for (int wi = 0; wi < 8; wi++) {
            uint64_t v = wq[wi];
            if (v) IOLog("  [0x%02x] 0x%016llx\n", wi * 8, v);
        }
    }

    return kIOReturnTimeout;
}
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
    uint32_t bcrStart = readReg32(FALCON_BCR_CTRL);
    bool booterDone = false;
    for (int i = 0; i < 5000; i++) {
        IOSleep(2);
        uint32_t bcr = readReg32(FALCON_BCR_CTRL);
        uint32_t cpuctl = readReg32(FALCON_CPUCTL);
        // Booter sets BCR_CTRL = CORE_SELECT_RISCV when switching to RISC-V
        // On GA104, VALID bit (0x1) may not be set by the Booter
        if ((bcr & 0x10) == 0x10) {
            IOLog("GA104: Booter done after %dms (BCR=0x%08x CPUCTL=0x%08x)\n",
                  i * 2, bcr, cpuctl);
            booterDone = true;
            break;
        }
        if ((i % 1000) == 999) {
            IOLog("GA104:   still waiting... BCR=0x%08x CPUCTL=0x%08x\n",
                  bcr, cpuctl);
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

