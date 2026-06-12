// GA104GSPRPC.cpp — GA104Device display implementation
#include "GA104Device.hpp"
#include "GA104DeviceUtilities.h"
#include "GA104Regs.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <string.h>

#define super IOService


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
    /* wpDoor unused */
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
            (void)mPre; // quiet unused warning
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


// MCTP over EMEM: send GSP-FMC command to SEC2
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
        (void)mEntry; //
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


IOReturn GA104Device::sendGspRpcAllocDisplayChain()
{
    if (!fGSPProtocol) return kIOReturnNotReady;
    IOLog("GA104: Allocating RM display objects...\n");

    // 1. ALLOC_DEVICE (NV01_DEVICE_0)
    GspRpcMessageHeader msg, reply;
    uint32_t replySz = 0;
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));

    // Use NVKM_RM handles from GSPProtocol.hpp
    NvHandle hClient = NVKM_RM_DEVICE;   // client = device handle
    NvHandle hDevice = NVKM_RM_SUBDEVICE; // device handle
    NvHandle hSubdevice = 0x5D1D0001;    // subdevice handle
    NvHandle hDisp = NVKM_RM_DISP;       // display handle

    fGSPProtocol->buildAllocDevice(&msg, hClient, hDevice, 0);
    uint32_t payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    IOReturn ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader), payloadSz,
        &reply, sizeof(reply), &replySz, 10000);
    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *out = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmRoot = 0;
        fRmDevice = out->hObject;
        setProperty("GA104RM_Device", (uint64_t)fRmDevice, 32);
        IOLog("GA104: ALLOC_DEVICE -> handle 0x%x\n", fRmDevice);
    } else {
        IOLog("GA104: ALLOC_DEVICE failed (rpc=0x%x, ret=0x%x)\n", reply.rpcResult, ret);
        return ret;
    }

    // 2. ALLOC_SUBDEVICE
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));
    fGSPProtocol->buildAllocSubdevice(&msg, hClient, fRmDevice, hSubdevice);
    payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader), payloadSz,
        &reply, sizeof(reply), &replySz, 10000);
    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *out = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmSubdevice = out->hObject;
        setProperty("GA104RM_Subdevice", (uint64_t)fRmSubdevice, 32);
        IOLog("GA104: ALLOC_SUBDEVICE -> handle 0x%x\n", fRmSubdevice);
    } else {
        IOLog("GA104: ALLOC_SUBDEVICE failed (rpc=0x%x, ret=0x%x)\n", reply.rpcResult, ret);
        return ret;
    }

    // 3. ALLOC_DISP (NV0073)
    bzero(&msg, sizeof(msg)); bzero(&reply, sizeof(reply));
    fGSPProtocol->buildAllocDisp(&msg, hClient, fRmSubdevice, hDisp, 0x0F, 0x0F);
    payloadSz = msg.length - sizeof(GspRpcMessageHeader);
    ret = sendGspRpc(&msg,
        (uint8_t*)&msg + sizeof(GspRpcMessageHeader), payloadSz,
        &reply, sizeof(reply), &replySz, 10000);
    if (ret == kIOReturnSuccess && reply.rpcResult == 0) {
        GspRmAllocParams *out = (GspRmAllocParams*)((uint8_t*)&reply + sizeof(GspRpcMessageHeader));
        fRmDisp = out->hObject;
        setProperty("GA104RM_Disp", (uint64_t)fRmDisp, 32);
        IOLog("GA104: ALLOC_DISP -> handle 0x%x\n", fRmDisp);
    } else {
        IOLog("GA104: ALLOC_DISP failed (rpc=0x%x, ret=0x%x)\n", reply.rpcResult, ret);
        return ret;
    }

    setProperty("GA104_RM_ChainDone", true);
    IOLog("GA104: RM display chain allocated successfully\n");
    return kIOReturnSuccess;
}

