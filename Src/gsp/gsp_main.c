#include "gsp_main.h"
#include "gsp_obj.h"
#include <IOKit/IOLib.h>

NV_STATUS gsp_init(OBJGPU *pGpu)
{
    if (!pGpu) return NV_ERR_INVALID_ARGUMENT;

    IOLog("GSP: init starting\n");

    // 1. Allocate GSP arguments descriptor (sysmem, cached)
    //    This holds GSP_ARGUMENTS_CACHED → MESSAGE_QUEUE_INIT_ARGUMENTS
    // 2. Allocate LibOS init arguments descriptor
    // 3. Setup message queues
    // 4. Populate WPR meta
    // 5. Prepare system info and registry RPC data
    //
    // In the byte-for-byte port, these will be calls to:
    //   kgspAllocBootArgs_HAL(pGpu, pKernelGsp)
    //   GspMsgQueuesInit(pGpu, &pMQCollection)
    //   kgspPopulateWprMeta_HAL(pGpu, pKernelGsp)
    //   _kgspPrepareSystemInfo(pGpu, pKernelGsp)
    //   _kgspPrepareRegistry(pGpu, pKernelGsp)

    IOLog("GSP: init done\n");
    return NV_OK;
}

NV_STATUS gsp_boot(OBJGPU *pGpu)
{
    if (!pGpu) return NV_ERR_INVALID_ARGUMENT;

    IOLog("GSP: boot starting\n");

    // 1. kgspBootstrap_HAL(pGpu, pKernelGsp)
    //    → kflcnResetIntoRiscv (GSP)
    //    → kgspProgramLibosBootArgsAddr (GSP MAILBOX)
    //    → kgspExecuteBooterLoad (SEC2)
    //    → kgspSendInitRpcs
    //    → kgspWaitForRmInitDone
    //
    // The fNDA's bootSEC2() already does much of this.
    // In byte-for-byte port, we replace it with kernel_gsp.c's implementation.

    IOLog("GSP: boot done\n");
    return NV_OK;
}

void gsp_shutdown(OBJGPU *pGpu)
{
    if (!pGpu) return;
    gsp_obj_destroy(pGpu);
    IOLog("GSP: shutdown\n");
}
