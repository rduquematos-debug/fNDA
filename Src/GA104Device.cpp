#include "GA104Device.hpp"
#include "GA104Regs.h"
#include "GA104DeviceUtilities.h"
#include "GA104FBProvider.hpp"
#include "GA104Framebuffer.hpp"
#include "GA104UserClient.hpp"
#include "GSPFirmwareParser.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <kern/thread_call.h>
#include <IOKit/IOCommandGate.h>
#define super IOService
OSDefineMetaClassAndStructors(GA104Device, IOService);

bool GA104Device::init(OSDictionary *dict)
{
    if (!super::init(dict)) return false;
    IOLog("GA104: init() called\n");

    fProvider = nullptr;
    fGSPFirmware = nullptr;
    fGSPQueue = nullptr;
    fGSPProtocol = nullptr;
    fFBProvider = nullptr;
    fVBIOSDisplay = nullptr;
    fBAR0Map = nullptr;
    fBAR1Map = nullptr;
    fBAR2Map = nullptr;
    fFWBuffer = nullptr;
    fFWBufferPhys = 0;
    fFWBufferSize = 0;
    fBootloaderBuffer = nullptr;
    fBootloaderPhys = 0;
    fBootloaderSize = 0;
    fGSPBase = nullptr;
    fBar0Virt = nullptr;
    fBar0Phys = 0;
    fBar1Virt = nullptr;
    fBar1Phys = 0;
    fBar1Size = 0;
    fBar2Phys = 0;
    fVRAMBase = nullptr;
    fDeviceID = 0;
    fRevision = 0;
    fVRAMSize = 0;
    fBDF = 0;
    fGSPBooted = false;
    fSEC2Booted = false;
    memset(&fVramLayout, 0, sizeof(fVramLayout));
    fWprMetaBuf = nullptr; fWprMetaPhys = 0; fWprAddr = 0; fLastFwVal = 0;
    fSec2SigBuf = nullptr; fSec2SigPhys = 0;
    fRadix3Buf = nullptr; fRadix3Phys = 0;
    fLibosBuf = nullptr; fLibosPhys = 0;
    fShmBuf = nullptr; fShmPhys = 0;
    fLogInitBuf = nullptr; fLogInitPhys = 0;
    fLogIntrBuf = nullptr; fLogIntrPhys = 0;
    fLogRmBuf = nullptr; fLogRmPhys = 0;
    fRmargsBuf = nullptr; fRmargsPhys = 0;
    fFwEntryPoint = 0;
    fHasCOTPayload = false;
    memset(fCOTPayload, 0, 864);
    fCmdqTx = nullptr; fMsgqTx = nullptr;
    fCmdqEntryBase = nullptr; fMsgqEntryBase = nullptr;
    fVramCmdqEntryBase = nullptr; fVramMsgqEntryBase = nullptr;
    fCmdqOff = 0; fMsgqOff = 0;
    fLastMsgqRp = 0;
    fFwImageData = nullptr; fFwImageSize = 0;
    fBooterV3 = false;
    fBooterImemSz = 0; fBooterDmemSz = 0;
    fBooterManifestOff = 0; fBooterEngMask = 0;
    fBooterUcode = 0; fBooterAppVer = 0;
    return true;
}
void GA104Device::free()
{
    OSSafeReleaseNULL(fGSPFirmware);
    OSSafeReleaseNULL(fGSPQueue);
    OSSafeReleaseNULL(fGSPProtocol);
    OSSafeReleaseNULL(fVBIOSDisplay);
    if (fBAR0Map) { fBAR0Map->release(); fBAR0Map = nullptr; }
    if (fBAR1Map) { fBAR1Map->release(); fBAR1Map = nullptr; }
    if (fBAR2Map) { fBAR2Map->release(); fBAR2Map = nullptr; }
    if (fFWBuffer) { IOFreeAligned(fFWBuffer, fFWBufferSize); fFWBuffer = nullptr; }
    if (fBootloaderBuffer) { IOFreeAligned(fBootloaderBuffer, fBootloaderSize); fBootloaderBuffer = nullptr; }
    cleanupPhase2();
    fProvider = nullptr;
    super::free();
}
bool GA104Device::start(IOService *provider)
{
    IOLog("GA104: start() called with provider %s\n",
          provider ? provider->getName() : "(null)");
    if (!super::start(provider)) return false;
    
    setProperty("GA104Started", true);
    setProperty("GA104Build", "v72g-lilu");

    fProvider = provider;
    fProvider->retain();
    IOLog("GA104: Provider retained, reading PCI identity from IORegistry\n");

    if (identifyDevice() != kIOReturnSuccess) return false;
    IOLog("GA104: Device identified, enabling PCI resources\n");

    IOPCIDevice *pciDev = findPCIDeviceAncestor(fProvider);
    if (pciDev) {
        pciDev->setIOEnable(true);
        pciDev->setBusMasterEnable(true);
        IOLog("GA104: PCI bus mastering and IO enabled\n");
    } else {
        IOLog("GA104: WARNING - could not find IOPCIDevice ancestor for PCI cfg writes\n");
    }

    // Mapear BARs (pode falhar em VFIO)
    // NOTE: Do not access GPU registers here - causes panic in VFIO.
    // O acesso a registos e feito apenas via UserClient.
    IOReturn barRet = mapBars();
    if (barRet == kIOReturnSuccess && fBar0Virt) {
        IOLog("GA104: BARs mapped OK (stub mode - no register access)\n");
        fVRAMSize = fBar1Size;
        setProperty("GA104BAR0Size", (uint64_t)fBAR0Map->getLength(), 64);
        setProperty("GA104BAR1Size", fBar1Size, 64);
    } else {
        IOLog("GA104: BARs not available, running in stub mode\n");
        fVRAMSize = 0x200000000ULL;
    }
    setProperty("VRAM,totalMB", (uint64_t)(fVRAMSize / (1024 * 1024)), 32);

    IOLog("GA104: Device started in stub mode — use UserClient for GPU operations\n");

    // Auto-boot GSP via IOTimerEventSource (delayed to avoid kernel panic)
    // Fallback: bootGSP() will be called from userspace loader

    // Initialize display engine (bare metal with BAR access)
    if (fBar0Virt && fBar1Virt) {
        IOReturn dispRet = setupFramebuffer();
        if (dispRet == kIOReturnSuccess) {
            setupDisplayChannels();
            legacyDisplayInit();
            IOLog("GA104: Display initialized — FB at 0x%llx\n", fFB.fbAddr);
        } else {
            IOLog("GA104: Framebuffer setup failed, skipping display init\n");
        }
    } else {
        IOLog("GA104: BARs not mapped, skipping display init\n");
    }

    // Create framebuffer provider as a child nub
    GA104FBProvider *fb = new GA104FBProvider;
    if (fb && fb->init(this)) {
        if (fb->start(this)) {
            fFBProvider = fb;
            fFBProvider->retain();
            IOLog("GA104: FBProvider started as child nub\n");
        } else {
            IOLog("GA104: FBProvider start failed\n");
            fb->release();
        }
    } else {
        if (fb) fb->release();
        IOLog("GA104: FBProvider init failed\n");
    }

    registerService();
    IOLog("GA104: Device started (ID: 0x%04x, Rev: 0x%02x)\n", fDeviceID, fRevision);
    return true;
}
void GA104Device::stop(IOService *provider)
{
    IOLog("GA104: Device stopping\n");
    if (fFBProvider) { fFBProvider->stop(this); fFBProvider->release(); fFBProvider = nullptr; }
    OSSafeReleaseNULL(fGSPFirmware);
    OSSafeReleaseNULL(fGSPQueue);
    OSSafeReleaseNULL(fGSPProtocol);
    OSSafeReleaseNULL(fVBIOSDisplay);
    if (fBAR0Map) { fBAR0Map->release(); fBAR0Map = nullptr; }
    if (fBAR1Map) { fBAR1Map->release(); fBAR1Map = nullptr; }
    if (fBAR2Map) { fBAR2Map->release(); fBAR2Map = nullptr; }
    if (fFWBuffer) { IOFreeAligned(fFWBuffer, fFWBufferSize); fFWBuffer = nullptr; }
    if (fProvider) { fProvider->release(); fProvider = nullptr; }
    super::stop(provider);
}
IOReturn GA104Device::identifyDevice()
{
    if (!fProvider) return kIOReturnNoDevice;
    uint16_t vendorID = readPropertyU16(fProvider, "vendor-id");
    uint32_t deviceIDraw = readPropertyU32(fProvider, "device-id");
    fDeviceID = deviceIDraw & 0xFFFF;
    fRevision = readPropertyU8(fProvider, "revision-id");
    if (vendorID != 0x10DE) {
        IOLog("GA104: Not an NVIDIA device (Vendor: 0x%04x)\n", vendorID);
        return kIOReturnUnsupported;
    }
    IOLog("GA104: Found NVIDIA GPU (Device: 0x%04x, Rev: 0x%02x) via IORegistry\n", fDeviceID, fRevision);
    setProperty("GA104VendorID", vendorID, 16);
    setProperty("GA104DeviceID", fDeviceID, 16);
    setProperty("GA104RevisionID", fRevision, 8);
    if (fBar0Virt) {
        uint32_t boot0 = reinterpret_cast<volatile uint32_t*>(fBar0Virt + NV_PMC_BOOT_0)[0];
        uint32_t arch = (boot0 >> 20) & 0xF;
        setProperty("GA104PMCBoot0", boot0, 32);
        setProperty("GA104Arch", arch, 8);
        IOLog("GA104: Chip BOOT_0=0x%08x arch=0x%x\n", boot0, arch);
    }
    return kIOReturnSuccess;
}
IOReturn GA104Device::mapBars()
{
    if (!fProvider) return kIOReturnNoDevice;
    IOPCIDevice *pciDev = findPCIDeviceAncestor(fProvider);
    if (!pciDev) {
        IOLog("GA104: No IOPCIDevice ancestor\n");
        return kIOReturnNotFound;
    }

    // BAR0: MMIO
    fBAR0Map = pciDev->mapDeviceMemoryWithIndex(0);
    if (!fBAR0Map) { IOLog("GA104: Failed to map BAR0\n"); return kIOReturnNoMemory; }
    fBar0Virt = (uint8_t*)fBAR0Map->getVirtualAddress();
    fBar0Phys = fBAR0Map->getPhysicalSegment(0, nullptr);
    fGSPBase = fBar0Virt + NV_PGSP_BASE;
    setProperty("GA104BAR0Phys", fBar0Phys, 64);
    setProperty("GA104BAR0Size", (uint64_t)fBAR0Map->getLength(), 64);
    setProperty("GA104BAR0Virt", (uint64_t)(uintptr_t)fBar0Virt, 64);
    IOLog("GA104: BAR0 mapped (virt=%p, phys=0x%llx)\n", fBar0Virt, fBar0Phys);

    // BAR1: VRAM — enumerate indices to find the largest mapping
    for (int bi = 0; bi <= 5; bi++) {
        IOMemoryDescriptor *md = pciDev->getDeviceMemoryWithIndex(bi);
        if (md) {
            IOMemoryMap *tm = md->createMappingInTask(kernel_task, 0, kIOMapAnywhere, 0, 0);
            if (tm) {
                uint64_t sz = tm->getLength();
                if (sz > fBar1Size) {
                    if (fBAR1Map) fBAR1Map->release();
                    fBAR1Map = tm; fBar1Size = sz;
                } else tm->release();
            }
        }
    }
    if (fBAR1Map) {
        fBar1Virt = (uint8_t*)fBAR1Map->getVirtualAddress();
        fBar1Phys = fBAR1Map->getPhysicalSegment(0, nullptr);
        setProperty("GA104BAR1Phys", fBar1Phys, 64);
        setProperty("GA104BAR1Size", fBar1Size, 64);
        IOLog("GA104: BAR1 mapped (size=0x%llx)\n", fBar1Size);
        fVRAMBase = fBar1Virt; fVRAMSize = fBar1Size;
    } else {
        IOLog("GA104: BAR1 not available\n");
    }

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
IOReturn GA104Device::loadGSPFirmware()
{
    fGSPFirmware = new GSPFirmware;
    if (!fGSPFirmware) return kIOReturnNoMemory;
    if (!fGSPFirmware->init()) { OSSafeReleaseNULL(fGSPFirmware); return kIOReturnUnsupported; }
    IOLog("GA104: GSPFirmware created\n");
    return kIOReturnSuccess;
}
IOReturn GA104Device::newUserClient(task_t owningTask, void *securityID,
                                     UInt32 type, OSDictionary *properties,
                                     IOUserClient **handler)
{
    GA104UserClient *client = new GA104UserClient;
    if (!client) return kIOReturnNoMemory;
    if (!client->init(nullptr)) { client->release(); return kIOReturnNoMemory; }
    if (!client->start(this)) { client->release(); return kIOReturnNoMemory; }
    *handler = client;
    return kIOReturnSuccess;
}
uint32_t GA104Device::readReg32(uint32_t offset)
{
    if (!fGSPBase) return 0;
    return *(volatile uint32_t*)(fGSPBase + offset);
}
void GA104Device::writeReg32(uint32_t offset, uint32_t value)
{
    if (!fGSPBase) return;
    *(volatile uint32_t*)(fGSPBase + offset) = value;
}
uint32_t GA104Device::writeAbsReg32(uint32_t absOffset, uint32_t value)
{
    if (!fBar0Virt) return 0;
    *(volatile uint32_t*)(fBar0Virt + absOffset) = value;
    __sync_synchronize();
    return *(volatile uint32_t*)(fBar0Virt + absOffset);
}
uint32_t GA104Device::readAbsReg32(uint32_t absOffset)
{
    if (!fBar0Virt) return 0;
    return *(volatile uint32_t*)(fBar0Virt + absOffset);
}
IOReturn GA104Device::writeVRAM(uint64_t vramOff, const void *data, uint32_t size)
{
    if (!fBar1Phys || !data || size == 0) return kIOReturnNotReady;
    if (vramOff + size > fBar1Size) return kIOReturnNoSpace;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
        fBar1Phys + vramOff, size, kIODirectionOut);
    if (!md) return kIOReturnNoMemory;
    md->prepare(); md->writeBytes(0, data, size); md->complete(); md->release();
    __sync_synchronize();
    return kIOReturnSuccess;
}
IOReturn GA104Device::readVRAM(uint64_t vramOff, void *data, uint32_t size)
{
    if (!fBar1Phys || !data || size == 0) return kIOReturnNotReady;
    if (vramOff + size > fBar1Size) return kIOReturnNoSpace;
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
        fBar1Phys + vramOff, size, kIODirectionIn);
    if (!md) return kIOReturnNoMemory;
    md->prepare(); md->readBytes(0, data, size); md->complete(); md->release();
    return kIOReturnSuccess;
}
uint32_t GA104Device::readCoreGET()
{
    return readReg32(NV_PDISP_CORE_GET);
}
void GA104Device::writeCorePUT(uint32_t dwordOffset)
{
    writeReg32(NV_PDISP_CORE_PUT, dwordOffset);
    __sync_synchronize();
}

