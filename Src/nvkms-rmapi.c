// nvkms-rmapi.c — RM API adapter (port from nvidia-src nvkms-rmapi-dgpu.c)
// Provides nvRmApi* functions that pack params into RMOps struct and call nvkms_call_rm()

#include "fnda_nvkms_interface.h"

// Minimal NVOS parameter structures matching nvidia_kernel_rmapi_ops_t layout
// See: nvidia-src/src/nvidia/arch/nvalloc/unix/include/nv-kernel-rmapi-ops.h
// These match the structures in fnda_nvkms_interface.cpp's nvkms_call_rm()

typedef struct {
    NvU32 hRoot, hObjectParent, hObjectNew, hClass;
    NvU64 pAllocParms;
    NvU32 status, paramsSize;
    NvU32 flags;
} NVOS64_PARAMETERS;

typedef struct {
    NvU32                    hRoot;
    NvU32                    hObjectParent;
    NvU32                    hObjectNew;
    NvU32                    hClass;
    NvU32                    flags;
    NvU64                    pMemory;
    NvU64                    limit;
    NvU32                    status;
    NvU32                    pad0;
} NVOS02_PARAMETERS;

typedef struct {
    NvU32                    hRoot;
    NvU32                    hObjectParent;
    NvU32                    hObjectOld;
    NvU32                    status;
    NvU32                    flags;
} NVOS00_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hObject;
    NvU32                    cmd;
    NvU64                    params;
    NvU32                    paramsSize;
    NvU32                    status;
    NvU32                    flags;
} NVOS54_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hParent;
    NvU32                    hObject;
    NvU32                    hClientSrc;
    NvU32                    hObjectSrc;
    NvU32                    flags;
    NvU32                    status;
} NVOS55_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hDevice;
    NvU32                    hMemory;
    NvU64                    offset;
    NvU64                    length;
    NvU64                    pLinearAddress;
    NvU32                    flags;
    NvU32                    status;
} NVOS33_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hDevice;
    NvU32                    hMemory;
    NvU64                    pLinearAddress;
    NvU32                    flags;
    NvU32                    status;
} NVOS34_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hDevice;
    NvU32                    hDma;
    NvU32                    hMemory;
    NvU64                    offset;
    NvU64                    length;
    NvU64                    dmaOffset;
    NvU32                    flags;
    NvU32                    status;
} NVOS46_PARAMETERS;

typedef struct {
    NvU32                    hClient;
    NvU32                    hDevice;
    NvU32                    hDma;
    NvU32                    hMemory;
    NvU64                    dmaOffset;
    NvU32                    flags;
    NvU32                    status;
} NVOS47_PARAMETERS;

#define NV_PTR_TO_NvP64(p)   ((NvU64)(uintptr_t)(p))
#define NvP64_VALUE(p)       ((void*)(uintptr_t)(p))

#define NV04_ALLOC             0x00008004
#define NV04_CONTROL           0x0000800E
#define NV01_FREE              0x00000001
#define NV01_ALLOC_MEMORY      0x00000002
#define NV04_MAP_MEMORY        0x00008010
#define NV04_UNMAP_MEMORY      0x00008011
#define NV04_MAP_MEMORY_DMA    0x00008012
#define NV04_UNMAP_MEMORY_DMA  0x00008017
#define NV04_DUP_OBJECT        0x0000800F

typedef struct {
    NvU32 op;
    union {
        NVOS00_PARAMETERS    free;
        NVOS02_PARAMETERS    allocMemory64;
        NVOS64_PARAMETERS    alloc;
        NVOS54_PARAMETERS    control;
        NVOS55_PARAMETERS    dupObject;
        NVOS33_PARAMETERS    mapMemory;
        NVOS34_PARAMETERS    unmapMemory;
        NVOS46_PARAMETERS    mapMemoryDma;
        NVOS47_PARAMETERS    unmapMemoryDma;
    } params;
} nvidia_kernel_rmapi_ops_t;

// ---- Implementations (direct port from nvkms-rmapi-dgpu.c) ----

NvU32 nvRmApiAlloc(NvU32 hClient, NvU32 hParent, NvU32 hObject,
                   NvU32 hClass, void *pAllocParams)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_ALLOC;
    ops.params.alloc.hRoot         = hClient;
    ops.params.alloc.hObjectParent = hParent;
    ops.params.alloc.hObjectNew    = hObject;
    ops.params.alloc.hClass        = hClass;
    ops.params.alloc.pAllocParms   = NV_PTR_TO_NvP64(pAllocParams);
    nvkms_call_rm(&ops);
    return ops.params.alloc.status;
}

NvU32 nvRmApiAllocMemory64(NvU32 hClient, NvU32 hParent, NvU32 hMemory,
                           NvU32 hClass, NvU32 flags,
                           void **ppAddress, NvU64 *pLimit)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV01_ALLOC_MEMORY;
    ops.params.allocMemory64.hRoot         = hClient;
    ops.params.allocMemory64.hObjectParent = hParent;
    ops.params.allocMemory64.hObjectNew    = hMemory;
    ops.params.allocMemory64.hClass        = hClass;
    ops.params.allocMemory64.flags         = flags;
    ops.params.allocMemory64.pMemory       = NV_PTR_TO_NvP64(*ppAddress);
    ops.params.allocMemory64.limit         = *pLimit;
    nvkms_call_rm(&ops);
    *pLimit    = ops.params.allocMemory64.limit;
    *ppAddress = NvP64_VALUE(ops.params.allocMemory64.pMemory);
    return ops.params.allocMemory64.status;
}

NvU32 nvRmApiControl(NvU32 hClient, NvU32 hObject, NvU32 cmd,
                     void *pParams, NvU32 paramsSize)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_CONTROL;
    ops.params.control.hClient    = hClient;
    ops.params.control.hObject    = hObject;
    ops.params.control.cmd        = cmd;
    ops.params.control.params     = NV_PTR_TO_NvP64(pParams);
    ops.params.control.paramsSize = paramsSize;
    nvkms_call_rm(&ops);
    return ops.params.control.status;
}

NvU32 nvRmApiDupObject2(NvU32 hClient, NvU32 hParent, NvU32 *hObjectDest,
                        NvU32 hClientSrc, NvU32 hObjectSrc, NvU32 flags)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_DUP_OBJECT;
    ops.params.dupObject.hClient    = hClient;
    ops.params.dupObject.hParent    = hParent;
    ops.params.dupObject.hObject    = *hObjectDest;
    ops.params.dupObject.hClientSrc = hClientSrc;
    ops.params.dupObject.hObjectSrc = hObjectSrc;
    ops.params.dupObject.flags      = flags;
    nvkms_call_rm(&ops);
    *hObjectDest = ops.params.dupObject.hObject;
    return ops.params.dupObject.status;
}

NvU32 nvRmApiDupObject(NvU32 hClient, NvU32 hParent, NvU32 hObjectDest,
                       NvU32 hClientSrc, NvU32 hObjectSrc, NvU32 flags)
{
    NvU32 hObjectLocal = hObjectDest;
    return nvRmApiDupObject2(hClient, hParent, &hObjectLocal,
                             hClientSrc, hObjectSrc, flags);
}

NvU32 nvRmApiFree(NvU32 hClient, NvU32 hParent, NvU32 hObject)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV01_FREE;
    ops.params.free.hRoot         = hClient;
    ops.params.free.hObjectParent = hParent;
    ops.params.free.hObjectOld    = hObject;
    nvkms_call_rm(&ops);
    return ops.params.free.status;
}

NvU32 nvRmApiVidHeapControl(void *pVidHeapControlParams)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_ALLOC;
    ops.params.alloc.pAllocParms = NV_PTR_TO_NvP64(pVidHeapControlParams);
    nvkms_call_rm(&ops);
    return 0;
}

NvU32 nvRmApiMapMemory(NvU32 hClient, NvU32 hDevice, NvU32 hMemory,
                       NvU64 offset, NvU64 length,
                       void **ppLinearAddress, NvU32 flags)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_MAP_MEMORY;
    ops.params.mapMemory.hClient = hClient;
    ops.params.mapMemory.hDevice = hDevice;
    ops.params.mapMemory.hMemory = hMemory;
    ops.params.mapMemory.offset  = offset;
    ops.params.mapMemory.length  = length;
    ops.params.mapMemory.flags   = flags;
    nvkms_call_rm(&ops);
    *ppLinearAddress = NvP64_VALUE(ops.params.mapMemory.pLinearAddress);
    return ops.params.mapMemory.status;
}

NvU32 nvRmApiUnmapMemory(NvU32 hClient, NvU32 hDevice, NvU32 hMemory,
                         const void *pLinearAddress, NvU32 flags)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_UNMAP_MEMORY;
    ops.params.unmapMemory.hClient        = hClient;
    ops.params.unmapMemory.hDevice        = hDevice;
    ops.params.unmapMemory.hMemory        = hMemory;
    ops.params.unmapMemory.pLinearAddress = NV_PTR_TO_NvP64(pLinearAddress);
    ops.params.unmapMemory.flags          = flags;
    nvkms_call_rm(&ops);
    return ops.params.unmapMemory.status;
}

NvU32 nvRmApiMapMemoryDma(NvU32 hClient, NvU32 hDevice, NvU32 hDma,
                          NvU32 hMemory, NvU64 offset, NvU64 length,
                          NvU32 flags, NvU64 *pDmaOffset)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_MAP_MEMORY_DMA;
    ops.params.mapMemoryDma.hClient   = hClient;
    ops.params.mapMemoryDma.hDevice   = hDevice;
    ops.params.mapMemoryDma.hDma      = hDma;
    ops.params.mapMemoryDma.hMemory   = hMemory;
    ops.params.mapMemoryDma.offset    = offset;
    ops.params.mapMemoryDma.length    = length;
    ops.params.mapMemoryDma.dmaOffset = *pDmaOffset;
    ops.params.mapMemoryDma.flags     = flags;
    nvkms_call_rm(&ops);
    *pDmaOffset = ops.params.mapMemoryDma.dmaOffset;
    return ops.params.mapMemoryDma.status;
}

NvU32 nvRmApiUnmapMemoryDma(NvU32 hClient, NvU32 hDevice, NvU32 hDma,
                            NvU32 hMemory, NvU32 flags, NvU64 dmaOffset)
{
    nvidia_kernel_rmapi_ops_t ops; bzero(&ops, sizeof(ops));
    ops.op = NV04_UNMAP_MEMORY_DMA;
    ops.params.unmapMemoryDma.hClient   = hClient;
    ops.params.unmapMemoryDma.hDevice   = hDevice;
    ops.params.unmapMemoryDma.hDma      = hDma;
    ops.params.unmapMemoryDma.hMemory   = hMemory;
    ops.params.unmapMemoryDma.flags     = flags;
    ops.params.unmapMemoryDma.dmaOffset = dmaOffset;
    nvkms_call_rm(&ops);
    return ops.params.unmapMemoryDma.status;
}
