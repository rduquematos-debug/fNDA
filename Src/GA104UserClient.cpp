#include "GA104UserClient.hpp"
#include <IOKit/IOLib.h>

#define super IOUserClient
OSDefineMetaClassAndStructors(GA104UserClient, IOUserClient)

const IOExternalMethodDispatch GA104UserClient::sMethods[kGA104MethodCount] = {
    { sGetGSPStatus,  0, 0, 2, 0 },
    { sLoadFirmware,  0, 0xffffffff, 0xffffffff, 0 },
    { sBootGSP,       0, 0, 1, 0 },
    { sGetBARInfo,    0, 0, 6, 0 },
    { sReadReg,       1, 0, 1, 0 },
    { sWriteReg,      2, 0, 0, 0 },
    { sGetDeviceInfo, 0, 0, 3, 0 },
    { sFWAppendChunk, 1, 0, 1, 0 },
    { sFinalize,      1, 0, 1, 0 },
    { sLoadBootloader, 1, 0, 1, 0 },
    { sBLBufferAlloc,  1, 0, 1, 0 },
    { sBootMainFirmware, 0, 0xffffffff, 1, 0 },
    { sInitDisplay,     0, 0,          1, 0 },
    { sSetCOTPayload,   1, 0xffffffff, 0, 0 },
    { sBootSEC2,        0, 0,          1, 0 },
    { sFillFramebuffer,    1, 0,       3, 0 },
    { sFlipToTriangle,     0, 0,       3, 0 },
    { sFlipToFramebuffer,  0, 0,       3, 0 },
    { sReadCSRs,           0, 0,       3, 0 },
    { sWriteVRAM,          2, 0xffffffff, 0, 0 }, // scalar=[offset,size], structIn=data
    { sReadVRAM,           2, 0,          0, 0xffffffff }, // scalar=[offset,size], structOut=data
    { sReadCoreGET,        0, 0,          1, 0 },
    { sWriteCorePUT,       1, 0,          0, 0 },
    { sGetCorePbAddr,      0, 0,          1, 0 },
};

bool GA104UserClient::init(OSDictionary *dict)
{
    if (!super::init(dict)) return false;
    fDevice = nullptr;
    return true;
}

void GA104UserClient::free()
{
    fDevice = nullptr;
    super::free();
}

bool GA104UserClient::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    fDevice = OSDynamicCast(GA104Device, provider);
    if (!fDevice) return false;
    return true;
}

void GA104UserClient::stop(IOService *provider)
{
    fDevice = nullptr;
    super::stop(provider);
}

IOReturn GA104UserClient::clientClose(void)
{
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
                                               IOMemoryDescriptor **memory)
{
    if (!fDevice) return kIOReturnNotAttached;

    uint64_t barPhys = 0;
    uint64_t barSize = 0;

    if (type == 0) {
        barPhys = fDevice->getBAR2Phys();
        barSize = fDevice->getBAR2Size();
    } else if (type == 1) {
        barPhys = fDevice->getBAR1Phys();
        barSize = fDevice->getBAR1Size();
    }

    if (barPhys && barSize) {
        IOMemoryDescriptor *mem = IOMemoryDescriptor::withPhysicalAddress(
            (IOPhysicalAddress)barPhys, (IOByteCount)barSize, kIODirectionInOut);
        if (mem) {
            *memory = mem;
            *options = 0;
            return kIOReturnSuccess;
        }
    }

    if (type == 2) {
        // Expose firmware buffer for userspace memcpy
        void *fwBuf = fDevice->getFWBufferAddr();
        uint32_t fwSz = fDevice->getFWBufferSize();
        if (fwBuf && fwSz) {
            IOMemoryDescriptor *mem = IOMemoryDescriptor::withAddressRange(
                (mach_vm_address_t)fwBuf, fwSz, kIODirectionInOut, kernel_task);
            if (mem) {
                *memory = mem;
                *options = 0;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnNoMemory;
    }

    if (type == 3) {
        // Expose bootloader buffer for userspace memcpy
        void *blBuf = fDevice->getBootloaderAddr();
        uint32_t blSz = fDevice->getBootloaderSize();
        if (blBuf && blSz) {
            IOMemoryDescriptor *mem = IOMemoryDescriptor::withAddressRange(
                (mach_vm_address_t)blBuf, blSz, kIODirectionInOut, kernel_task);
            if (mem) {
                *memory = mem;
                *options = 0;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnNoMemory;
    }

    if (type == 4) {
        // Expose framebuffer for userspace direct pixel access
        uint64_t fbPhys = fDevice->getBAR1Phys() + fDevice->getFramebufferAddr();
        uint64_t fbSize = fDevice->getFramebufferSize();
        if (fbPhys && fbSize > 0) {
            IOMemoryDescriptor *mem = IOMemoryDescriptor::withPhysicalAddress(
                (IOPhysicalAddress)fbPhys, (IOByteCount)fbSize, kIODirectionInOut);
            if (mem) {
                *memory = mem;
                *options = 0;
                return kIOReturnSuccess;
            }
        }
        return kIOReturnNoMemory;
    }

    return super::clientMemoryForType(type, options, memory);
}

IOReturn GA104UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
                                          IOExternalMethodDispatch *dispatch, OSObject *target, void *reference)
{
    if (selector >= kGA104MethodCount)
        return kIOReturnUnsupported;

    dispatch = (IOExternalMethodDispatch *)&sMethods[selector];
    target = this;
    reference = nullptr;

    if (fDevice) {
        if (selector == 2) fDevice->setProperty("GA104ExtMeth2", true);
        if (selector == 1) fDevice->setProperty("GA104ExtMeth1", true);
        if (selector == 10) fDevice->setProperty("GA104ExtMeth10", true);
        if (selector == 9) fDevice->setProperty("GA104ExtMeth9", true);
    }

    IOReturn extRet = super::externalMethod(selector, args, dispatch, target, reference);
    if (selector == 10 || selector == 9) {
        IOLog("GA104: extMethod sel=%u ret=0x%x\n", selector, extRet);
    }
    return extRet;
}

#pragma mark - Method Implementations

IOReturn GA104UserClient::sGetGSPStatus(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    uint32_t status = me->fDevice->readReg32(FALCON_CPUCTL);
    uint32_t mailbox0 = me->fDevice->readReg32(FALCON_MAILBOX0);

    args->scalarOutput[0] = status;
    args->scalarOutput[1] = mailbox0;
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sLoadFirmware(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    // If no struct input, finalize from existing FW buffer
    if (!args->structureInput || args->structureInputSize == 0) {
        void *buf = me->fDevice->getFWBufferAddr();
        uint32_t sz = me->fDevice->getFWBufferSize();
        me->fDevice->setProperty("GA104FinalBuf", (uint64_t)(uintptr_t)buf, 64);
        me->fDevice->setProperty("GA104FinalSz", (uint64_t)sz, 64);
        if (!buf || sz == 0) return kIOReturnNotReady;

        IOReturn r = me->fDevice->loadGSPFirmware();
        me->fDevice->setProperty("GA104FinalLoad", (uint64_t)r, 32);
        if (r != kIOReturnSuccess) return r;

        r = me->fDevice->getGSPFirmware()->loadExternal(buf, sz);
        me->fDevice->setProperty("GA104FinalExt", (uint64_t)r, 32);
        return r;
    }

    IOReturn ret = me->fDevice->loadGSPFirmware();
    if (ret != kIOReturnSuccess) return ret;

    return me->fDevice->getGSPFirmware()->loadFromData(
        (void *)args->structureInput, args->structureInputSize);
}

IOReturn GA104UserClient::sFinalize(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    me->fDevice->setProperty("GA104FinHit", true);

    void *buf = me->fDevice->getFWBufferAddr();
    uint32_t sz = me->fDevice->getFWBufferSize();
    me->fDevice->setProperty("GA104FinBuf", (uint64_t)(uintptr_t)buf, 64);
    me->fDevice->setProperty("GA104FinSz", (uint64_t)sz, 64);

    if (!buf || sz == 0) { args->scalarOutput[0] = 0; return kIOReturnNotReady; }

    IOReturn ret = me->fDevice->loadGSPFirmware();
    me->fDevice->setProperty("GA104FinLoad", (uint64_t)ret, 32);
    if (ret != kIOReturnSuccess) {
    args->scalarOutput[0] = ret;
    return ret;
}

    ret = me->fDevice->getGSPFirmware()->loadExternal(buf, sz);
    me->fDevice->setProperty("GA104FinExt", (uint64_t)ret, 32);
    args->scalarOutput[0] = ret;
    return ret;
}

IOReturn GA104UserClient::sLoadBootloader(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    uint32_t size = (uint32_t)args->scalarInput[0];
    void *buf = me->fDevice->getBootloaderAddr();
    if (!buf || size == 0 || size > me->fDevice->getBootloaderSize())
        return kIOReturnBadArgument;

    me->fDevice->setProperty("GA104BL_Size", size, 32);
    IOLog("GA104: bootloader loaded: %u bytes (via shared buffer)\n", size);

    // SEC2 booter handles booter load internally (bootSEC2 -> bootGSP)
    // This method is kept for compatibility but booter DMA is now done by bootGSP
    IOLog("GA104: bootloader stored (SEC2 booter will load it via bootSEC2)\n");
    return kIOReturnSuccess;
}

// ---- End of UserClient methods ----

IOReturn GA104UserClient::sBootMainFirmware(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    // Replaced by bootGSP() — kept as alias for backward compatibility
    IOLog("GA104: sBootMainFirmware deprecated, use sBootGSP instead\n");
    args->scalarOutput[0] = kIOReturnUnsupported;
    return kIOReturnUnsupported;
}

IOReturn GA104UserClient::sInitDisplay(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    // EFI GOP clock preserved in start(). Just write our VRAM address to HEAD_BASE.
    me->fDevice->writeReg32(NV_PHEAD_SET_BASE(0),
        (uint32_t)(me->fDevice->getFramebufferAddr() & 0xFFFFFFFF));
    me->fDevice->writeReg32(NV_PHEAD_SET_BASE_LIGHT(0),
        (uint32_t)(me->fDevice->getFramebufferAddr() & 0xFFFFFFFF));
    me->fDevice->writeReg32(NV_PWINDOW_SET_BASE(0),
        (uint32_t)(me->fDevice->getFramebufferAddr() & 0xFFFFFFFF));

    args->scalarOutput[0] = kIOReturnSuccess;
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sBLBufferAlloc(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) { IOLog("GA104: sBLBufferAlloc - no device\n"); return kIOReturnNotAttached; }
    uint32_t size = (uint32_t)args->scalarInput[0];
    IOLog("GA104: sBLBufferAlloc size=%u siCnt=%u soCnt=%u\n", size, args->scalarInputCount, args->scalarOutputCount);
    IOReturn ret = me->fDevice->createBLBuffer(size);
    IOLog("GA104: sBLBufferAlloc ret=%d\n", ret);
    return ret;
}

IOReturn GA104UserClient::sBootGSP(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    me->fDevice->setProperty("GA104BSHit", true);

    void *buf = me->fDevice->getFWBufferAddr();
    uint32_t sz = me->fDevice->getFWBufferSize();
    me->fDevice->setProperty("GA104BSBuf", (uint64_t)(uintptr_t)buf, 64);
    me->fDevice->setProperty("GA104BSSz", (uint64_t)sz, 64);

    if (buf && sz > 0 && (!me->fDevice->getGSPFirmware() || !me->fDevice->getGSPFirmware()->isLoaded())) {
        IOReturn r = me->fDevice->loadGSPFirmware();
        me->fDevice->setProperty("GA104BSLoad", (uint64_t)r, 32);
        if (r == kIOReturnSuccess) {
            // Debug: read first 16 bytes from buffer
            if (buf && sz >= 16) {
                uint32_t *dw = (uint32_t*)buf;
                me->fDevice->setProperty("GA104BufMagic0", dw[0], 32);
                me->fDevice->setProperty("GA104BufMagic1", dw[1], 32);
                me->fDevice->setProperty("GA104BufMagic2", dw[2], 32);
                me->fDevice->setProperty("GA104BufMagic3", dw[3], 32);
            }
            r = me->fDevice->getGSPFirmware()->loadExternal(buf, sz);
            me->fDevice->setProperty("GA104BSLoadExt", (uint64_t)r, 32);
            if (r != kIOReturnSuccess) { args->scalarOutput[0] = r; return r; }
        }
    }

    IOReturn ret = me->fDevice->bootGSP();
    me->fDevice->setProperty("GA104BSRet", (uint64_t)ret, 32);
    args->scalarOutput[0] = ret;
    return ret;
}

IOReturn GA104UserClient::sGetBARInfo(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    args->scalarOutput[0] = me->fDevice->getBAR0Phys();
    args->scalarOutput[1] = me->fDevice->getBAR0Size();
    args->scalarOutput[2] = me->fDevice->getBAR1Phys();
    args->scalarOutput[3] = me->fDevice->getBAR1Size();
    args->scalarOutput[4] = me->fDevice->getBAR2Phys();
    args->scalarOutput[5] = me->fDevice->getBAR2Size();
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sReadReg(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    uint64_t absOffset = args->scalarInput[0];
    args->scalarOutput[0] = me->fDevice->readAbsReg32((uint32_t)absOffset);
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sWriteReg(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    uint64_t absOffset = args->scalarInput[0];
    uint32_t value  = (uint32_t)args->scalarInput[1];
    me->fDevice->writeAbsReg32((uint32_t)absOffset, value);
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sGetDeviceInfo(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;

    args->scalarOutput[0] = 0x10DE;
    args->scalarOutput[1] = me->fDevice->getDeviceID();
    args->scalarOutput[2] = me->fDevice->getRevision();
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sFWAppendChunk(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    uint32_t size = (uint32_t)args->scalarInput[0];
    IOReturn ret = me->fDevice->createFWBuffer(size);
    args->scalarOutput[0] = (ret == kIOReturnSuccess) ? size : 0;
    return ret;
}

IOReturn GA104UserClient::sSetCOTPayload(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    uint32_t size = (uint32_t)args->scalarInput[0];
    if (!args->structureInput || size == 0) {
        me->fDevice->setCOTPayload(nullptr, 0);
        return kIOReturnSuccess;
    }
    return me->fDevice->setCOTPayload((const uint8_t*)args->structureInput, size);
}

IOReturn GA104UserClient::sBootSEC2(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    IOReturn ret = me->fDevice->bootSEC2();
    args->scalarOutput[0] = (uint64_t)ret;
    return ret;
}

IOReturn GA104UserClient::sFillFramebuffer(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    uint32_t color = (uint32_t)args->scalarInput[0];
    IOReturn ret = me->fDevice->fillFramebuffer(color);
    args->scalarOutput[0] = (uint64_t)ret;
    args->scalarOutput[1] = color;
    args->scalarOutput[2] = 0;
    return ret;
}

IOReturn GA104UserClient::sFlipToTriangle(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    IOReturn ret = me->fDevice->flipToTriangle();
    args->scalarOutput[0] = (uint64_t)ret;
    args->scalarOutput[1] = 0;
    args->scalarOutput[2] = 0;
    return ret;
}

IOReturn GA104UserClient::sFlipToFramebuffer(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    IOReturn ret = me->fDevice->flipToFramebuffer();
    args->scalarOutput[0] = (uint64_t)ret;
    args->scalarOutput[1] = 0;
    args->scalarOutput[2] = 0;
    return ret;
}

IOReturn GA104UserClient::sReadCSRs(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    IOReturn ret = me->fDevice->readCSRs();
    args->scalarOutput[0] = (uint64_t)ret;
    args->scalarOutput[1] = 0;
    args->scalarOutput[2] = 0;
    return ret;
}

IOReturn GA104UserClient::sWriteVRAM(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    uint64_t vramOff = args->scalarInput[0];
    uint32_t size = (uint32_t)args->scalarInput[1];
    if (!args->structureInput || size == 0) return kIOReturnBadArgument;
    IOLog("GA104: WriteVRAM: off=0x%llx sz=%u\n", vramOff, size);
    return me->fDevice->writeVRAM(vramOff, args->structureInput, size);
}

IOReturn GA104UserClient::sReadVRAM(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    uint64_t vramOff = args->scalarInput[0];
    uint32_t size = (uint32_t)args->scalarInput[1];
    if (!args->structureOutput || size == 0) return kIOReturnBadArgument;
    IOLog("GA104: ReadVRAM: off=0x%llx sz=%u\n", vramOff, size);
    return me->fDevice->readVRAM(vramOff, args->structureOutput, size);
}

IOReturn GA104UserClient::sReadCoreGET(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    args->scalarOutput[0] = me->fDevice->readCoreGET();
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sWriteCorePUT(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    me->fDevice->writeCorePUT((uint32_t)args->scalarInput[0]);
    return kIOReturnSuccess;
}

IOReturn GA104UserClient::sGetCorePbAddr(OSObject *target, void *reference, IOExternalMethodArguments *args)
{
    GA104UserClient *me = OSDynamicCast(GA104UserClient, target);
    if (!me || !me->fDevice) return kIOReturnNotAttached;
    args->scalarOutput[0] = me->fDevice->getCorePbAddr();
    return kIOReturnSuccess;
}
