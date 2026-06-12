// Power management stubs for GSP driver
// These replace NVIDIA's power management HAL with no-ops.
#include "os_compat.h"

NV_STATUS kgspWaitForProcessorSuspend_HAL(OBJGPU *pGpu, KernelGsp *pKernelGsp)
{
    return NV_OK;
}

NV_STATUS kgspExecuteCoreResume_HAL(OBJGPU *pGpu, KernelGsp *pKernelGsp)
{
    return NV_OK;
}
