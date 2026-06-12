// GA104DeviceUtilities.h — Shared utility functions for GA104 driver
#ifndef GA104DeviceUtilities_h
#define GA104DeviceUtilities_h

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOService.h>

// Display pushbuffer sizes (defined in GA104Regs.h)
// #define DISP_PB_CORE_SIZE         0x10000
// #define DISP_PB_WINDOW_SIZE        0x10000

// SEC2 register access (PSEC2 = 0x00110000 + offset)
#define SEC2_REG(off, val) writeAbsReg32(NV_PSEC_BASE + (off), (val))

static inline uint32_t readPropertyU32(IOService *svc, const char *name)
{
    OSData *data = OSDynamicCast(OSData, svc->getProperty(name));
    if (!data || data->getLength() < 4) return 0;
    return *reinterpret_cast<const uint32_t*>(data->getBytesNoCopy());
}

static inline uint16_t readPropertyU16(IOService *svc, const char *name)
{
    OSData *data = OSDynamicCast(OSData, svc->getProperty(name));
    if (!data || data->getLength() < 2) return 0;
    return *reinterpret_cast<const uint16_t*>(data->getBytesNoCopy());
}

static inline uint8_t readPropertyU8(IOService *svc, const char *name)
{
    OSData *data = OSDynamicCast(OSData, svc->getProperty(name));
    if (!data || data->getLength() < 1) return 0;
    return *reinterpret_cast<const uint8_t*>(data->getBytesNoCopy());
}

static inline IOPCIDevice* findPCIDeviceAncestor(IOService *svc)
{
    IOService *current = svc;
    while (current) {
        IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, current);
        if (pci) return pci;
        current = current->getProvider();
    }
    return nullptr;
}

#endif /* GA104DeviceUtilities_h */
