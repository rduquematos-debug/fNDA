#include "gsp_obj.h"
#include <IOKit/IOLib.h>

NV_STATUS gsp_obj_init(OBJGPU *pGpu, GA104Device *device)
{
    if (!pGpu || !device) return NV_ERR_INVALID_ARGUMENT;

    pGpu->device = device;

    // Allocate KernelGsp
    pGpu->pKernelGsp = (KernelGsp*)IOMallocAligned(sizeof(KernelGsp), 64);
    if (!pGpu->pKernelGsp) return NV_ERR_NO_MEMORY;
    bzero(pGpu->pKernelGsp, sizeof(KernelGsp));
    pGpu->pKernelGsp->pGpu = pGpu;
    pGpu->pKernelGsp->falcon.pGpu = pGpu;

    // Allocate KernelSec2
    pGpu->pKernelSec2 = (KernelSec2*)IOMallocAligned(sizeof(KernelSec2), 64);
    if (!pGpu->pKernelSec2) {
        IOFreeAligned(pGpu->pKernelGsp, sizeof(KernelGsp));
        pGpu->pKernelGsp = NULL;
        return NV_ERR_NO_MEMORY;
    }
    bzero(pGpu->pKernelSec2, sizeof(KernelSec2));
    pGpu->pKernelSec2->pGpu = pGpu;
    pGpu->pKernelSec2->falcon.pGpu = pGpu;

    // Chip info
    pGpu->regBase     = 0x110000; // NV_PGSP_BASE
    pGpu->chipArch    = 0x17;     // NV_CHIP_ARCH_AMPERE
    pGpu->chipImpl    = 0x104;    // GA104
    pGpu->pciDeviceId = device->getDeviceID();
    pGpu->pciRevision = device->getRevision();
    pGpu->fbSize      = device->getBAR2Size(); // VRAM size
    pGpu->pRpc        = NULL;

    // RPC object (OBJRPC) — simplified, allocated when needed
    // In a full port, this would be the NVOC OBJRPC instance

    IOLog("GSP: OBJGPU initialized (device=0x%04x rev=0x%02x fb=%llu MB)\n",
          pGpu->pciDeviceId, pGpu->pciRevision,
          (unsigned long long)(pGpu->fbSize / (1024 * 1024)));

    return NV_OK;
}

void gsp_obj_destroy(OBJGPU *pGpu)
{
    if (!pGpu) return;

    // Free sub-object memory descriptors
    if (pGpu->pKernelGsp) {
        KernelGsp *gsp = pGpu->pKernelGsp;
        if (gsp->pWprMetaDescriptor)
            memdescDestroy(&gsp->pWprMetaDescriptor);
        if (gsp->pLibosInitArgumentsDescriptor)
            memdescDestroy(&gsp->pLibosInitArgumentsDescriptor);
        if (gsp->pGspArgumentsDescriptor)
            memdescDestroy(&gsp->pGspArgumentsDescriptor);
        IOFreeAligned(gsp, sizeof(KernelGsp));
        pGpu->pKernelGsp = NULL;
    }

    if (pGpu->pKernelSec2) {
        IOFreeAligned(pGpu->pKernelSec2, sizeof(KernelSec2));
        pGpu->pKernelSec2 = NULL;
    }

    bzero(pGpu, sizeof(OBJGPU));
}
