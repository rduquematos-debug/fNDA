#ifndef os_compat_h
#define os_compat_h

// ============================================================================
// os_compat.h — Linux/nvidia.ko → macOS IOKit Compatibility Layer
//
// Purpose: Allow byte-for-byte inclusion of NVIDIA open-gpu-kernel-modules
// source files in the GA104Driver macOS kext.
//
// Each #define maps a Linux or NVIDIA internal API to its macOS IOKit
// equivalent. Non-essential subsystems (power, ECC, NVLink, SR-IOV) are
// stubbed out with dummy implementations.
//
// Sources mapped:
//   kernel_gsp.c / message_queue_cpu.c / kernel_gsp_ga102.c /
//   kernel_gsp_falcon_ga102.c / kernel_gsp_booter.c
// ============================================================================

#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <sys/types.h>
#include <mach/mach_time.h>
#include <string.h>
#include <libkern/OSAtomic.h>

// Include our register definitions and GSP protocol structs
#include "GA104Regs.h"

// Forward declarations (needed before OBJGPU/GA104Device are fully defined)
struct GA104Device;
struct OBJGPU;
struct ConfidentialCompute;

// Forward declaration — our IOService subclass
class GA104Device;

// ============================================================================
// 1. NVIDIA TYPE SYSTEM
// ============================================================================

typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef uint16_t NvU16;
typedef uint8_t  NvU8;
typedef int32_t  NvS32;
typedef int64_t  NvS64;
typedef NvU32    NV_STATUS;
typedef bool     NvBool;
// NvHandle is defined in GA104Regs.h as uint32_t — use that definition
typedef uint64_t RmPhysAddr;
typedef NvU64    NvLength;

#define NV_OK                       0
#define NV_ERR_GENERIC              0x000000FF
#define NV_ERR_NO_MEMORY            0x00000006
#define NV_ERR_INVALID_ARGUMENT     0x0000000D
#define NV_ERR_NOT_SUPPORTED        0x00000022
#define NV_ERR_TIMEOUT              0x0000004D
#define NV_ERR_STATE_IN_USE         0x00000052
#define NV_ERR_BAD_PARAM            0x0000002C
#define NV_ERR_OBJECT_TYPE_MISMATCH 0x0000002B
#define NV_ERR_INSUFFICIENT_PERMISSIONS 0x0000005D

#define NV_TRUE                     1
#define NV_FALSE                    0
#define NV_NULL                     NULL

// Common NVIDIA utility macros (from nvtypes.h / nvport)
#define NV_OFFSETOF(type, field)        offsetof(type, field)
#define NV_ALIGN_UP(x, a)              (((x) + ((a) - 1)) & ~((a) - 1))
#define NV_ALIGN_DOWN(x, a)            ((x) & ~((a) - 1))
#define NV_DIV_AND_CEIL(x, d)          (((x) + (d) - 1) / (d))
#define NV_MAX(a, b)                   ((a) > (b) ? (a) : (b))
#define NV_MIN(a, b)                   ((a) < (b) ? (a) : (b))
#define NV_ARRAY_SIZE(arr)             (sizeof(arr) / sizeof((arr)[0]))

// RM page macros
#define RM_PAGE_MASK                   (RM_PAGE_SIZE - 1)
#define RM_PAGE_ALIGN_UP(x)            NV_ALIGN_UP(x, RM_PAGE_SIZE)
#define RM_PAGE_ALIGN_DOWN(x)          NV_ALIGN_DOWN(x, RM_PAGE_SIZE)

// Address space types
#define NV_ADDRESS_IS_GPU_PHYSICAL(addrSpace)  ((addrSpace) == ADDR_FBMEM)

// Memory descriptor flags
#define MEMDESC_FLAGS_NONE              0

// NvP64 — pageable 64-bit pointer (on macOS, same as uintptr_t)
typedef uintptr_t NvUPtr;
typedef NvUPtr NvP64;

// IS_SILICON — we always run on real hardware
#define IS_SILICON(pGpu)                (NV_TRUE)

// Confidential Compute — disabled on consumer GPU
#define gpuIsCCFeatureEnabled(pGpu)     (NV_FALSE)

// REF_VAL — used to reference register field enum values, just return the value
#define REF_VAL(val)                    (val)

// 64-bit helper macros
#define NvU64_LO32(val)  ((NvU32)((val) & 0xFFFFFFFFULL))
#define NvU64_HI32(val)  ((NvU32)(((val) >> 32) & 0xFFFFFFFFULL))

// GPU instance helpers
#define gpuGetInstance(pGpu)            (0U)
#define gpuGetDomain(pGpu)              (0U)
#define gpuGetBus(pGpu)                 (1U)
#define gpuGetDevice(pGpu)              (0U)
#define gpuGetChipArch(pGpu)            (0x17U)  // NV_CHIP_ARCH_AMPERE
#define gpuGetChipImpl(pGpu)            (0x104U) // GA104

// GPU mark for reset — stub
#define gpuMarkDeviceForReset(pGpu)     ((void)0)

// Registry key strings (minimal set)
#define NV_REG_STR_RM_GSP_STATUS_QUEUE_SIZE  "RMGspStatusQueueSize"

// ============================================================================
// 2. MMIO REGISTER I/O
// ============================================================================

// NVIDIA uses GPU_REG_RD32/WR32 for ALL hardware register access.
// pGpu is OBJGPU* (which wraps our GA104Device*).
// We access BAR0 (MMIO) via GA104Device::readAbsReg32/writeAbsReg32.
#define GPU_REG_RD32(pGpu, addr) \
    (((const OBJGPU*)(const void*)(pGpu))->device->readAbsReg32(addr))
#define GPU_REG_WR32(pGpu, addr, val) \
    (((OBJGPU*)(void*)(pGpu))->device->writeAbsReg32(addr, val))

// NVIDIA register base address extraction (from published/ headers)
// NV_PGSP range: 0x00110000:0x00113FFF
#define NV_PGSP_LOW_FIELD                   0x00110000
#define NV_PGSP_HIGH_FIELD                  0x00113FFF

// DRF_BASE: extract base from a register range definition
#define DRF_BASE(drf)                       (drf##_LOW_FIELD)

// Register field helper macros (NVIDIA DRF = Device Register Field)
// Simplified versions — not used by existing fNDA code, only by future NVIDIA .c ports

// Falcon v4 register field definitions (from dev_falcon_v4.h)
// Required by kernel_gsp_falcon_ga102.c for DMA + BROM operations.
// Format: SHIFT = bit position, MASK = bitmask, *_TRUE/*_FALSE/*_VAL = field value.

// DMATRFCMD fields
#define NV_PFALCON_FALCON_DMATRFCMD_FULL_SHIFT          0
#define NV_PFALCON_FALCON_DMATRFCMD_FULL_MASK           0x00000001U
#define NV_PFALCON_FALCON_DMATRFCMD_FULL_TRUE           0x00000001U
#define NV_PFALCON_FALCON_DMATRFCMD_FULL_FALSE          0x00000000U

#define NV_PFALCON_FALCON_DMATRFCMD_IDLE_SHIFT          1
#define NV_PFALCON_FALCON_DMATRFCMD_IDLE_MASK           0x00000002U
#define NV_PFALCON_FALCON_DMATRFCMD_IDLE_TRUE           0x00000001U
#define NV_PFALCON_FALCON_DMATRFCMD_IDLE_FALSE          0x00000000U

#define NV_PFALCON_FALCON_DMATRFCMD_SEC_SHIFT           2
#define NV_PFALCON_FALCON_DMATRFCMD_SEC_MASK            0x0000000CU

#define NV_PFALCON_FALCON_DMATRFCMD_IMEM_SHIFT          4
#define NV_PFALCON_FALCON_DMATRFCMD_IMEM_MASK           0x00000010U
#define NV_PFALCON_FALCON_DMATRFCMD_IMEM_TRUE           0x00000001U
#define NV_PFALCON_FALCON_DMATRFCMD_IMEM_FALSE          0x00000000U

#define NV_PFALCON_FALCON_DMATRFCMD_WRITE_SHIFT         5
#define NV_PFALCON_FALCON_DMATRFCMD_WRITE_MASK          0x00000020U
#define NV_PFALCON_FALCON_DMATRFCMD_WRITE_TRUE          0x00000001U
#define NV_PFALCON_FALCON_DMATRFCMD_WRITE_FALSE         0x00000000U

#define NV_PFALCON_FALCON_DMATRFCMD_SIZE_SHIFT          8
#define NV_PFALCON_FALCON_DMATRFCMD_SIZE_MASK           0x00000700U
#define NV_PFALCON_FALCON_DMATRFCMD_SIZE_256B           0x00000006U

#define NV_PFALCON_FALCON_DMATRFCMD_CTXDMA_SHIFT        12
#define NV_PFALCON_FALCON_DMATRFCMD_CTXDMA_MASK         0x00007000U

#define NV_PFALCON_FALCON_DMATRFCMD_SET_DMTAG_SHIFT     16
#define NV_PFALCON_FALCON_DMATRFCMD_SET_DMTAG_MASK      0x00010000U
#define NV_PFALCON_FALCON_DMATRFCMD_SET_DMTAG_TRUE      0x00000001U

// DMATRFBASE / DMATRFMOFFS / DMATRFFBOFFS fields
#define NV_PFALCON_FALCON_DMATRFBASE_BASE_SHIFT         0
#define NV_PFALCON_FALCON_DMATRFBASE_BASE_MASK          0xFFFFFF00U
#define NV_PFALCON_FALCON_DMATRFMOFFS_OFFS_SHIFT        0
#define NV_PFALCON_FALCON_DMATRFMOFFS_OFFS_MASK         0x00FFFFFFU
#define NV_PFALCON_FALCON_DMATRFFBOFFS_OFFS_SHIFT       0
#define NV_PFALCON_FALCON_DMATRFFBOFFS_OFFS_MASK        0xFFFFFFFFU
#define NV_PFALCON_FALCON_DMATRFBASE1_BASE_SHIFT        0
#define NV_PFALCON_FALCON_DMATRFBASE1_BASE_MASK         0x000001FFU

// FBIF TRANSCFG fields (from dev_fbif_v4.h)
#define NV_PFALCON_FBIF_TRANSCFG_TARGET_SHIFT           0
#define NV_PFALCON_FBIF_TRANSCFG_TARGET_MASK            0x00000003U
#define NV_PFALCON_FBIF_TRANSCFG_TARGET_LOCAL_FB        0x00000000U
#define NV_PFALCON_FBIF_TRANSCFG_TARGET_COHERENT_SYSMEM 0x00000001U

#define NV_PFALCON_FBIF_TRANSCFG_MEM_TYPE_SHIFT         2
#define NV_PFALCON_FBIF_TRANSCFG_MEM_TYPE_MASK          0x00000004U
#define NV_PFALCON_FBIF_TRANSCFG_MEM_TYPE_PHYSICAL      0x00000001U

#define NV_PFALCON_FBIF_TRANSCFG_ENGINE_ID_FLAG_SHIFT   16
#define NV_PFALCON_FBIF_TRANSCFG_ENGINE_ID_FLAG_MASK    0x00010000U

// BROM fields (from dev_falcon_second_pri.h)
#define NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID_VAL_SHIFT 0
#define NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID_VAL_MASK  0x000000FFU

// DRF macros — match NVIDIA nvmisc.h conventions
// DRF_SHIFT extracts field bit position from NV define
// DRF_MASK extracts field width mask
#define DRF_SHIFT(drf)      (drf##_SHIFT)
#define DRF_MASK(drf)       (drf##_MASK)
#define DRF_NUM_MOD(d,r,f,n)  (((NvU32)(n)) << DRF_SHIFT(NV##d##r##f))
#define DRF_DEF(d,r,f,c)    (((NvU32)(NV##d##r##f##c)) << DRF_SHIFT(NV##d##r##f))
#define DRF_SHIFTMASK(drf)  (DRF_MASK(drf) << (DRF_SHIFT(drf)))
#define FLD_SET_DRF_MOD(mod, dev, reg, field, val, dword) \
    (((dword) & ~(DRF_SHIFTMASK(NV##mod##dev##reg##field))) | DRF_DEF(mod, dev, reg, field, val))
#define FLD_SET_DRF_NUM(mod, dev, reg, field, val, dword) \
    (((dword) & ~(DRF_SHIFTMASK(NV##mod##dev##reg##field))) | DRF_NUM_MOD(mod, dev, reg, field, val))
#define FLD_SET_DRF(d,r,f,c,v)  FLD_SET_DRF_MOD(d,r,f,c,v)
#define GPU_REG_RD32_NV(pGpu, d, r, f) 0  // stub for register field reads

// Confidential Compute stub (needed by nvidia/*.c ports)
#define PDB_PROP_CONFCOMPUTE_CC_FEATURE_ENABLED         0x1
struct ConfidentialCompute {
    void *pRpcCcslCtx;
    NvBool (*getProperty)(struct ConfidentialCompute *pCC, NvU32 prop);
};

// ============================================================================
// 3. PORT LAYER (Memory, Atomic, String)
// ============================================================================

#define portMemAllocNonPaged(sz)                IOMalloc(sz)
#define portMemAllocPaged(sz)                   IOMalloc(sz)
#define portMemFree(ptr)                        IOFree(ptr, 0)
#define portMemSet(ptr, val, len)               memset(ptr, (int)(val), (size_t)(len))
#define portMemCopy(dst, dstSz, src, srcSz)     memcpy(dst, src, MIN(dstSz, srcSz))
#define portMemCopyAligned(dst, dstSz, src, srcSz, align) \
    memcpy(dst, src, MIN(dstSz, srcSz))

#define portStringCopy(dst, dstSz, src, srcSz)  strncpy(dst, src, MIN(dstSz, srcSz))
#define portStringCat(dst, dstSz, src, srcSz)   strncat(dst, src, MIN(dstSz, srcSz))
#define portStringLength(str)                   strlen(str)
#define portStringCompare(s1, s2, n)            strncmp(s1, s2, n)

#define portAtomicMemoryFenceFull()             __sync_synchronize()
#define portAtomicMemoryFenceLoad()             __sync_synchronize()
#define portAtomicMemoryFenceStore()            __sync_synchronize()

static inline NvS32 portAtomicCompareAndSwapS32(NvS32 volatile *ptr,
                                                 NvS32 oldVal, NvS32 newVal)
{
    return OSCompareAndSwap((SInt32)oldVal, (SInt32)newVal, ptr)
           ? oldVal : *ptr;
}

static inline NvU32 portSafeMulU32(NvU32 a, NvU32 b, NvU32 *result)
{
    *result = a * b;
    return NV_OK;
}

// Debug printf — wraps to IOLog on macOS
#define portDbgPrintf(fmt, ...)                 IOLog(fmt, ##__VA_ARGS__)

// ============================================================================
// 4. OS ABSTRACTION LAYER
// ============================================================================

#define osGetTimestamp()                        mach_absolute_time()
#define osSpinLoop()                            /* no-op on x86_64 macOS */

static inline NvU64 osGetTimestampFreq(void)
{
    // mach_absolute_time() on x86_64 macOS returns nanoseconds
    return 1000000000ULL;
}

#define osDelay(ms)                             IOSleep(ms)

// Bugcheck — fatal error, should never be hit in normal operation
#define osBugCheck(code)                        do { IOLog("BUGCHECK: 0x%x\n", (unsigned)(code)); } while(0)

// Assert failure
#define osAssertFailed()                        do { IOLog("ASSERT FAILED at %s:%d\n", __FILE__, __LINE__); } while(0)

// Event notification — stub (GSP uses polling, not events)
#define osNotifyEvent(pGpu, pNotify, tag, h, data)

// Registry read/write — built on IORegistryEntry::getProperty
static inline NvBool osReadRegistryDword(OBJGPU *pGpu, const char *key, NvU32 *val)
{
    (void)pGpu; (void)key; (void)val;
    return NV_FALSE;
}

#define osWriteRegistryDword(pGpu, key, val)    ((void)(val))

// Work queue — stub. GSP health checks run via polling timer.
#define osQueueWorkItem(pGpu, func, params, flags)

// Get max userspace VA (for amd64 macOS: 47-bit)
#define osGetMaxUserVa()                        (0x7FFFFFFFFFFFULL)

// Host page size
#define osGetPageSize()                         (4096)

// ============================================================================
// 5. MEMORY DESCRIPTOR (memdesc*) — SIMPLIFIED
// ============================================================================
//
// NVIDIA's MEMORY_DESCRIPTOR is a complex abstraction over dma_alloc_coherent.
// For the GSP, we only need:
//   - Allocate physically contiguous (or page-describable) system memory
//   - Get physical address(es)
//   - Map to kernel virtual address
//
// We implement just enough to satisfy kernel_gsp.c and message_queue_cpu.c.

// Address space types (simplified)
#define ADDR_SYSMEM     0
#define ADDR_FBMEM      1

// Cache attributes (simplified)
#define NV_MEMORY_CACHED        0
#define NV_MEMORY_UNCACHED      1

// Memory descriptor — thin wrapper around IOMallocAligned
typedef struct MEMORY_DESCRIPTOR {
    void          *va;           // kernel virtual address
    RmPhysAddr     pa;           // physical address (first page)
    NvU64          size;         // total size in bytes
    NvU32          pageSize;     // page size hint (0 = 4K default)
    NvU8           addrSpace;    // ADDR_SYSMEM or ADDR_FBMEM
    NvU8           cacheAttr;    // NV_MEMORY_CACHED or NV_MEMORY_UNCACHED
    NvBool         contiguous;   // physically contiguous?

    // Physical address table (one entry per page)
    // Populated by memdescGetPhysAddrs
    RmPhysAddr    *pageTable;
    NvU32          pageCount;
} MEMORY_DESCRIPTOR;

// memdescCreate — allocate + get phys addr
static inline NV_STATUS memdescCreate(MEMORY_DESCRIPTOR **ppDesc,
                                       void *pGpu,
                                       NvU64 size,
                                       NvU64 align,
                                       NvBool contiguous,
                                       NvU8 addrSpace,
                                       NvU8 cacheAttrib,
                                       NvU32 flags)
{
    if (!ppDesc || size == 0) return NV_ERR_INVALID_ARGUMENT;

    MEMORY_DESCRIPTOR *desc = (MEMORY_DESCRIPTOR*)IOMalloc(sizeof(MEMORY_DESCRIPTOR));
    if (!desc) return NV_ERR_NO_MEMORY;

    bzero(desc, sizeof(*desc));

    // Allocate aligned memory
    NvU32 allocAlign = (align > 0) ? (NvU32)align : 4096;
    void *buf = IOMallocAligned((NvU32)size, allocAlign);
    if (!buf) { IOFree(desc, sizeof(*desc)); return NV_ERR_NO_MEMORY; }

    bzero(buf, (NvU32)size);

    // Get physical address via IOMemoryDescriptor
    IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(
        (mach_vm_address_t)buf, (IOByteCount)size,
        kIODirectionInOut, kernel_task);
    RmPhysAddr physAddr = 0;
    if (md) {
        md->prepare();
        physAddr = md->getPhysicalSegment(0, NULL);
        md->complete();
        md->release();
    }
    if (!physAddr) {
        IOFreeAligned(buf, (NvU32)size);
        IOFree(desc, sizeof(*desc));
        return NV_ERR_NO_MEMORY;
    }

    desc->va        = buf;
    desc->pa        = physAddr;
    desc->size      = size;
    desc->pageSize  = 4096;
    desc->addrSpace = addrSpace;
    desc->cacheAttr = cacheAttrib;
    desc->contiguous = contiguous;

    *ppDesc = desc;
    return NV_OK;
}

static inline NV_STATUS memdescMapInternal(void *pGpu,
                                            MEMORY_DESCRIPTOR *pDesc,
                                            NvU32 flags)
{
    // Already mapped — allocation returned kernel VA
    return NV_OK;
}

#define memdescUnmapInternal(pGpu, pDesc, flags)    ((void)0)

static inline void memdescFree(MEMORY_DESCRIPTOR *pDesc)
{
    if (!pDesc) return;
    if (pDesc->va)
        IOFreeAligned(pDesc->va, (NvU32)pDesc->size);
    if (pDesc->pageTable)
        IOFree(pDesc->pageTable, pDesc->pageCount * sizeof(RmPhysAddr));
}

#define memdescDestroy(ppDesc) \
    do { if (ppDesc && *ppDesc) { memdescFree(*ppDesc); IOFree(*ppDesc, sizeof(MEMORY_DESCRIPTOR)); *ppDesc = NULL; } } while(0)

static inline void *memdescGetKernelMapping(const MEMORY_DESCRIPTOR *pDesc)
{
    return pDesc ? pDesc->va : NULL;
}

#define memdescSetKernelMapping(pDesc, va)      ((void)0)
#define memdescGetKernelMappingPriv(pDesc)      (NULL)
#define memdescSetKernelMappingPriv(pDesc, p)   ((void)0)

static inline RmPhysAddr memdescGetPhysAddr(const MEMORY_DESCRIPTOR *pDesc,
                                              NvU8 addrTrans, NvU64 offset)
{
    if (!pDesc) return 0;
    // For contiguous allocations: phys = base + offset
    return pDesc->pa + offset;
}

static inline NvU64 memdescGetSize(const MEMORY_DESCRIPTOR *pDesc)
{
    return pDesc ? pDesc->size : 0;
}

#define memdescGetAddressSpace(pDesc)           ((pDesc)->addrSpace)
#define memdescGetCpuCacheAttrib(pDesc)         ((pDesc)->cacheAttr)

static inline NvBool memdescGetContiguity(const MEMORY_DESCRIPTOR *pDesc,
                                           NvU8 addrTrans)
{
    return pDesc ? pDesc->contiguous : NV_FALSE;
}

#define memdescSetPageSize(pDesc, trans, sz)    ((pDesc)->pageSize = (NvU32)(sz))

// memdescGetPhysAddrs — populate page table array (for msgq shared memory)
static inline NV_STATUS memdescGetPhysAddrs(const MEMORY_DESCRIPTOR *pDesc,
                                             NvU8 addrTrans,
                                             NvU64 offset,
                                             NvU64 stride,
                                             NvU32 count,
                                             RmPhysAddr *pPhysAddrTbl)
{
    if (!pDesc || !pPhysAddrTbl) return NV_ERR_INVALID_ARGUMENT;

    // For contiguous memory: each entry is base + offset + i * stride
    for (NvU32 i = 0; i < count; i++) {
        pPhysAddrTbl[i] = pDesc->pa + offset + i * stride;
    }
    return NV_OK;
}

#define memdescSetFlag(pDesc, flag, val)        ((void)0)
#define memdescDescribe(pDesc, addrSpace, off, sz)  ((void)0)
#define memdescSetCtxBufPool(pDesc, pPool)      ((void)0)

// ============================================================================
// 6. LOGGING / ASSERT MACROS
// ============================================================================

// NVIDIA log levels
#define LEVEL_INFO      0
#define LEVEL_ERROR     1
#define LEVEL_WARN      2
#define LEVEL_HARDWARE  3
#define LEVEL_SILENT    4
#define LEVEL_NOTIFICATION 5

#define MAKE_NV_PRINTF_STR(tag)     ((const char*)(tag))

#define NV_PRINTF(level, fmt, ...) \
    IOLog("GSP: " fmt "\n", ##__VA_ARGS__)

#define NV_PRINTF_COND(cond, l1, l2, fmt, ...) \
    do { if (cond) IOLog("GSP: " fmt "\n", ##__VA_ARGS__); } while(0)

#define NV_ASSERT(expr) \
    do { if (!(expr)) IOLog("GSP: ASSERT %s at %s:%d\n", #expr, __FILE__, __LINE__); } while(0)

#define NV_ASSERT_OR_RETURN(expr, ret) \
    do { if (!(expr)) { IOLog("GSP: ASSERT %s at %s:%d\n", #expr, __FILE__, __LINE__); return (ret); } } while(0)

#define NV_ASSERT_OR_RETURN_VOID(expr) \
    do { if (!(expr)) { IOLog("GSP: ASSERT %s at %s:%d\n", #expr, __FILE__, __LINE__); return; } } while(0)

#define NV_ASSERT_OK_OR_RETURN(expr) \
    do { NV_STATUS _s = (expr); if (_s != NV_OK) { IOLog("GSP: ASSERT_OK %s -> 0x%x at %s:%d\n", #expr, _s, __FILE__, __LINE__); return _s; } } while(0)

#define NV_ASSERT_OK_OR_GOTO(expr, label) \
    do { NV_STATUS _s = (expr); if (_s != NV_OK) { IOLog("GSP: ASSERT_OK %s -> 0x%x at %s:%d\n", #expr, _s, __FILE__, __LINE__); goto label; } } while(0)

#define NV_CHECK_OK_OR_RETURN(level, expr) \
    do { NV_STATUS _s = (expr); if (_s != NV_OK) return _s; } while(0)

#define NV_CHECK_OK_OR_GOTO(level, expr, label) \
    do { NV_STATUS _s = (expr); if (_s != NV_OK) goto label; } while(0)

#define NV_CHECK_OR_RETURN(level, expr, ret) \
    do { if (!(expr)) return (ret); } while(0)

#define NV_ASSERT_FAILED(msg) \
    IOLog("GSP: ASSERT FAILED: %s at %s:%d\n", msg, __FILE__, __LINE__)

// NVIDIA error logging
#define NV_ERROR_LOG_DATA(pGpu, errNum, fmt, ...) \
    IOLog("GSP: XID=%d " fmt "\n", (int)(errNum), ##__VA_ARGS__)

#define NV_ERROR_LOG_COND(pGpu, errNum, fmt, ...) \
    IOLog("GSP: XID=%d " fmt "\n", (int)(errNum), ##__VA_ARGS__)

// ============================================================================
// 7. GPU OBJECT (OBJGPU) — SIMPLIFIED
// ============================================================================
//
// The NVIDIA code passes OBJGPU* everywhere. We define a minimal version
// that wraps our GA104Device and provides access to the GSP/Falcon/SEC2
// sub-objects.

class KernelGsp;
class KernelFalcon;
class KernelSec2;

typedef struct OBJGPU {
    GA104Device   *device;         // our IOKit IOService
    KernelGsp     *pKernelGsp;     // GSP engine object
    KernelFalcon  *pKernelFalcon;  // Falcon base object
    KernelSec2    *pKernelSec2;    // SEC2 engine object

    // Physical BAR addresses (from PCI config space)
    NvU64          regBase;        // NV_PGSP_BASE = 0x110000

    // Chip identification
    NvU32          chipArch;       // NV_CHIP_ARCH_AMPERE = 0x17
    NvU32          chipImpl;       // 0x104 = GA104
    NvU32          pciDeviceId;    // 0x2482 = RTX 3070 Ti
    NvU32          pciRevision;    // Revision ID

    // Frame buffer size (usable VRAM)
    NvU64          fbSize;

    // RPC / message queue
    struct OBJRPC *pRpc;
} OBJGPU;

// NVOC static cast — in our simplified model, just a C-style cast
#define staticCast(obj, type)                   ((type*)(obj))
#define dynamicCast(obj, type)                  ((type*)(obj))

// GPU object accessors
#define GPU_GET_KERNEL_GSP(pGpu)                ((pGpu)->pKernelGsp)
#define GPU_GET_KERNEL_SEC2(pGpu)               ((pGpu)->pKernelSec2)
#define GPU_GET_KERNEL_FIFO(pGpu)               ((void*)NULL)
#define GPU_GET_CONF_COMPUTE(pGpu)              ((struct ConfidentialCompute*)0)
#define GPU_GET_PHYSICAL_RMAPI(pGpu)            ((void*)NULL)
#define GPU_GET_KERNEL_PERF(pGpu)               ((void*)NULL)
#define GPU_GET_KERNEL_NVLINK(pGpu)             ((void*)NULL)
#define GPU_GET_KERNEL_NVLINK_CORE(pGpu)        ((void*)NULL)
#define GPU_GET_KERNEL_BIF(pGpu)                ((void*)NULL)

// ============================================================================
// 8. KERNEL GSP / FALCON / SEC2 — SIMPLIFIED OBJECTS
// ============================================================================
//
// These are the objects that live "inside" the GPU and manage the GSP engine.
// We define minimal versions containing only fields accessed by the GSP code.

typedef struct KernelFalcon {
    OBJGPU      *pGpu;
    NvU32        regBase;            // e.g., NV_PGSP_BASE = 0x110000
    NvU32        riscvRegBase;       // e.g., NV_FALCON2_GSP_BASE = 0x111000
    NvBool       bBootFromHs;        // TRUE for GA102/GA104
    NvU32        physEngDesc;        // ENG_GSP or ENG_SEC2
} KernelFalcon;

typedef struct KernelSec2 {
    KernelFalcon  falcon;             // base class (NVIDIA uses NVOC inheritance)
    OBJGPU       *pGpu;
} KernelSec2;

// GSP booter ucode descriptor (from kernel_gsp_booter.c)
typedef struct KernelGspFlcnUcode {
    NvU32         bootType;           // KGSP_FLCN_UCODE_BOOT_FROM_HS etc.
    MEMORY_DESCRIPTOR *pUcodeMemDesc;
    NvU64         imemVa;             // IMEM virtual address (for BOOTVEC)
    NvU32         hsSigDmemAddr;      // DMEM offset of PKC signature
    NvU32         engineIdMask;
    NvU32         ucodeId;
} KernelGspFlcnUcode;

#define KGSP_FLCN_UCODE_BOOT_FROM_HS       0
#define KGSP_FLCN_UCODE_BOOT_WITH_LOADER   1
#define KGSP_FLCN_UCODE_BOOT_DIRECT        2

typedef struct KernelGsp {
    KernelFalcon  falcon;             // base class
    OBJGPU       *pGpu;

    // Boot ucode
    KernelGspFlcnUcode *pGspRmBootUcodeDesc;
    KernelGspFlcnUcode *pBooterLoadUcode;
    KernelGspFlcnUcode *pBooterUnloadUcode;
    KernelGspFlcnUcode *pScrubberUcode;

    // Memory descriptors
    MEMORY_DESCRIPTOR *pWprMetaDescriptor;
    MEMORY_DESCRIPTOR *pLibosInitArgumentsDescriptor;
    MEMORY_DESCRIPTOR *pGspArgumentsDescriptor;
    MEMORY_DESCRIPTOR *pCrashcatQueueMemDesc;

    // Boot args
    void    *pLibosInitArgumentsCached;
    void    *pGspArgumentsCached;

    // RPC
    struct OBJRPC *pRpc;
    struct MESSAGE_QUEUE_INFO *pMessageQueueInfo;

    // State
    NvBool   bGspRmInitialized;
    NvU8     gspFwCoreDumpReason;
} KernelGsp;

// ============================================================================
// 9. FALCON HAL — kflcn* MACROS
// ============================================================================
//
// These map directly to BAR0 register reads/writes at (regBase + regOffset).
// regBase is 0x110000 for GSP or 0x1a0000 for SEC2 on GA104.

#define kflcnRegRead_HAL(pGpu, pFlcn, reg) \
    GPU_REG_RD32(pGpu, ((const KernelFalcon*)(pFlcn))->regBase + (reg))

#define kflcnRegWrite_HAL(pGpu, pFlcn, reg, val) \
    GPU_REG_WR32(pGpu, ((const KernelFalcon*)(pFlcn))->regBase + (reg), (val))

#define kflcnRiscvRegWrite_HAL(pGpu, pFlcn, reg, val) \
    GPU_REG_WR32(pGpu, ((const KernelFalcon*)(pFlcn))->riscvRegBase + (reg), (val))

#define kflcnRiscvRegRead_HAL(pGpu, pFlcn, reg) \
    GPU_REG_RD32(pGpu, ((const KernelFalcon*)(pFlcn))->riscvRegBase + (reg))

// Falcon engine control helpers
#define kflcnReset_HAL(pGpu, pFlcn) \
    do { \
        NvU32 _eng = kflcnRegRead_HAL(pGpu, pFlcn, 0x03C0); \
        kflcnRegWrite_HAL(pGpu, pFlcn, 0x03C0, _eng | 0x01); \
        IODelay(10); \
        kflcnRegWrite_HAL(pGpu, pFlcn, 0x03C0, _eng & ~0x01); \
        IODelay(10); \
    } while(0)

#define kflcnStartCpu_HAL(pGpu, pFlcn) \
    kflcnRegWrite_HAL(pGpu, pFlcn, 0x0100, 0x02)  // CPUCTL = STARTCPU

#define kflcnWaitForHalt_HAL(pGpu, pFlcn, timeout, seq) \
    do { \
        NvU32 _cpuctl; \
        for (int _wi = 0; _wi < 5000; _wi++) { \
            _cpuctl = kflcnRegRead_HAL(pGpu, pFlcn, 0x0100); \
            if (_cpuctl & 0x10) break; \
            IODelay(2); \
        } \
    } while(0)

#define kflcnSetRiscvMode(b)                   ((void)0)
#define kflcnIsRiscvActive_HAL(pGpu, pFlcn)    (NV_TRUE)
#define kflcnIsRiscvMode(pGpu, pFlcn)          (NV_TRUE)
#define kflcnDisableCtxReq_HAL(pGpu, pFlcn)    ((void)0)
#define kflcnGetPendingHostInterrupts(pGpu, pFlcn)  (0)

// ============================================================================
// 10. GSP HAL — kgsp* STUBS
// ============================================================================
//
// These are HAL functions called by kernel_gsp.c. We implement the ones
// needed for GSP boot; non-essential ones are stubbed.

#define kgspHealthCheck_HAL(pGpu, pKernelGsp)           ((void)0)
#define kgspDumpGspLogs(pGpu, pKernelGsp)               ((void)0)
#define kgspDumpMailbox_HAL(pGpu, pKernelGsp)           ((void)0)

#define kgspSetCmdQueueHead_HAL(pGpu, pKernelGsp, idx, val) \
    GPU_REG_WR32(pGpu, 0x110C00 + (idx) * 8, (val))

#define kgspReadMailbox(pGpu, pKernelGsp, reg) \
    GPU_REG_RD32(pGpu, 0x110804 + (reg) * 4)

static inline NV_STATUS kgspConfigureFalcon_HAL(OBJGPU *pGpu,
                                                  KernelGsp *pKernelGsp)
{
    KernelFalcon *pFlcn = staticCast(pKernelGsp, KernelFalcon);
    pFlcn->regBase = 0x110000;          // NV_PGSP_BASE
    pFlcn->riscvRegBase = 0x111000;     // NV_FALCON2_GSP_BASE
    pFlcn->bBootFromHs = NV_TRUE;       // Ampere
    pFlcn->physEngDesc = 0x1E;          // ENG_GSP
    return NV_OK;
}

// ============================================================================
// 11. KERNEL FALCON ENGINE CONFIG
// ============================================================================

typedef struct {
    NvU32 registerBase;
    NvU32 riscvRegisterBase;
    NvU32 fbifBase;
    NvBool bBootFromHs;
    NvU32 pmcEnableMask;
    NvBool bIsPmcDeviceEngine;
    NvU32 physEngDesc;
    struct {
        NvBool  bEnable;
        const char *pName;
        NvU32   errorId;
        NvU32   allocQueueSize;
    } crashcatEngConfig;
} KernelFalconEngineConfig;

#define kflcnConfigureEngine(pGpu, pFlcn, pConfig)  ((void)0)
#define GSP_ERROR                                    0xBE

// ============================================================================
// 12. BINDATA (Firmware Archive) STUBS
// ============================================================================

typedef void BINDATA_STORAGE;
typedef void BINDATA_ARCHIVE;

#define BINDATA_LABEL_UCODE_IMAGE_DBG    "image_dbg"
#define BINDATA_LABEL_UCODE_DESC_DBG     "desc_dbg"
#define BINDATA_LABEL_UCODE_IMAGE_PROD   "image_prod"
#define BINDATA_LABEL_UCODE_DESC_PROD    "desc_prod"

#define bindataArchiveGetStorage(pArchive, label)     ((void*)(NULL))
#define bindataGetBufferSize(pStorage)                ((NvU32)0)
#define bindataWriteToBuffer(pStorage, buf, size)     ((NvU32)0)
#define bindataStorageAcquireData(pStorage, ppData)   ((void*)(NULL))
#define bindataStorageReleaseData(pData)              ((void)0)
#define BINDATA_LABEL_IMAGE_LOAD                      "image_load"
#define BINDATA_LABEL_HEADER_LOAD                     "hdr_load"
#define BINDATA_LABEL_SIG_LOAD                        "sig_load"

#define kgspGetBinArchiveBooterLoadUcode_HAL(pK)      ((void*)(NULL))
#define kgspGetBinArchiveBooterUnloadUcode_HAL(pK)    ((void*)(NULL))
#define kgspGetBinArchiveGspRmBoot_HAL(pK)            ((void*)(NULL))
#define kgspIsDebugModeEnabled(pGpu, pK)              (NV_FALSE)
#define kgspGetCrashcatSysmemBufferSize(pK)           ((NvU32)0x10000)

// ============================================================================
// 13. GPU TIMEOUT CONDITION WAIT
// ============================================================================

typedef NvBool (*GpuWaitConditionFunc)(void *pData);

#define gpuTimeoutCondWait(pGpu, pCondFunc, pData, pTimeout)  (NV_OK)
#define GPU_TIMEOUT_FLAGS_WAIT_INTR                        0

// Falcon constants
#define FLCN_BLK_ALIGNMENT            0x100
#define FLCN_DMEM_VA_INVALID          0xFFFFFFFFU
#define FLCN_ERR_BINARY_NOT_STARTED   (-1)

// DMA transfer constants (from kernel_gsp_falcon_ga102.c)
#define NV_PFALCON2_FALCON_BROM_PARAADDR(i)    (0x1210 + (i) * 4)
#define NV_PFALCON2_FALCON_BROM_ENGIDMASK      0x119C
#define NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID  0x1198
#define NV_PFALCON2_FALCON_MOD_SEL             0x0180

// GSP registers (from dev_gsp.h)
#define NV_PGSP_FBIF_BASE               0x110600

// GC6 island registers (stubs)
#define NV_PGSP_GC6_ISLAND_BASE         0x11A000

// RISC-V registers
#define NV_FALCON2_GSP_BASE             0x111000

// Private properties stub
#define GPU_GET_CONF_COMPUTE(pGpu)                  ((struct ConfidentialCompute*)0)

// ============================================================================
// 14. TIMEOUT / TIMER HELPERS
// ============================================================================

typedef struct {
    NvU64    timeoutAbs;
    NvU32    flags;
} RMTIMEOUT;

#define GPU_TIMEOUT_DEFAULT             { .flags = 0, .timeoutAbs = 0 }
#define GPU_TIMEOUT_FLAGS_OSTIMER                      0
#define GPU_TIMEOUT_FLAGS_DEFAULT                       0
#define GPU_TIMEOUT_FLAGS_BYPASS_THREAD_STATE           0
#define GPU_TIMEOUT_FLAGS_BYPASS_JOURNAL_LOG            0

static inline void gpuSetTimeout(OBJGPU *pGpu, NvU64 timeoutUs,
                                  RMTIMEOUT *pTimeout, NvU32 flags)
{
    pTimeout->timeoutAbs = mach_absolute_time() + timeoutUs;
    pTimeout->flags = flags;
}

static inline NvBool gpuCheckTimeout(OBJGPU *pGpu, RMTIMEOUT *pTimeout)
{
    return mach_absolute_time() < pTimeout->timeoutAbs ? NV_TRUE : NV_FALSE;
}

// ============================================================================
// 13. MCTP / NVDM PROTOCOL HELPERS
// ============================================================================

static inline NvU32 mctpCreateTransportHeader(NvU32 som, NvU32 eom,
                                               NvU32 seid, NvU32 deid,
                                               NvU32 seq)
{
    return (som << 31) | (eom << 30) | (seq << 28) |
           (seid << 16) | (deid << 8) | 0x01;
}

static inline NvU32 mctpCreateNvdmHeader(NvU32 type)
{
    return 0x7E | (0x10DE << 8) | (type << 24);
}

// ============================================================================
// 14. STUBS — Non-ported subsystems
// ============================================================================

// Power management
#define gpuIsGpuFullPowerForPmResume(pGpu)      (NV_TRUE)
#define kgspWaitForProcessorSuspend_HAL(pGpu,pK) (NV_OK)
#define kgspExecuteCoreResume_HAL(pGpu,pK)      (NV_OK)

// ECC
#define gpuCheckEccCounts_HAL(pGpu)             ((void)0)

// NVLink
#define knvlinkHandleFaultUpInterrupt_HAL(...)  ((void)0)
#define knvlinkInbandMsgCallbackDispatcher(...) ((void)0)
#define knvlinkSetDegradedMode(...)             ((void)0)

// Error/RC
#define krcErrorSendEventNotifications_HAL(...) ((void)0)
#define krcCheckBusError_HAL(...)               ((void)0)

// FIFO
#define kfifoGetChidMgr(...)                    (NULL)
#define kfifoChidMgrGetKernelChannel(...)       (NULL)

// Subdevice/device
#define subdeviceGetByInstance(...)             (NV_ERR_NOT_SUPPORTED)
#define gpumgrGetSubDeviceInstanceFromGpu(...)  (0)

// PMU
#define kpmuLogBuf(...)                         ((void)0)

// Timed sema
#define tsemaRelease_HAL(...)                   ((void)0)

// Dispatch
#define dispswReleaseSemaphoreAndNotifierFill(...) ((void)0)

// GSP FW sec
#define kgspGetFrtsSize_HAL(pGpu,pK)            ((NvU64)0)
#define kgspExecuteFwsec_HAL(pGpu,pK,pCmd)      (NV_OK)

// Registry
#define NV_REG_STR_RM_INTERNAL_SRIOV_CLX_FB_RESERVATION   ((const char*)0)

// ============================================================================
// 15. ENGINE DESCRIPTORS
// ============================================================================

typedef NvU32 ENGDESCRIPTOR;
#define ENG_GSP         0x1E
#define ENG_SEC2        0x1F

// ============================================================================
// 12. GSP MESSAGE QUEUE STRUCTURES (from message_queue.h)
// ============================================================================

// Queue element (from message_queue_priv.h)
typedef NvU8 GSP_MSG_QUEUE_ENCRYPTION_TAG[16];

typedef struct GSP_MSG_QUEUE_ELEMENT {
    NvU32 mctpHeader;
    NvU32 nvdmHeader;
    NvU32 checkSum;
    NvU32 seqNum;
    NvU8  payload[];
} __attribute__((packed)) GSP_MSG_QUEUE_ELEMENT;

// Queue info — what each side needs to manage one queue
typedef struct MESSAGE_QUEUE_INFO {
    // Queue dimensions
    NvLength commandQueueSize;
    NvLength statusQueueSize;
    NvLength queueElementHdrSize;
    NvLength queueElementSizeMin;
    NvLength queueElementSizeMax;
    NvU32 queueHeaderAlign;
    NvU32 queueElementAlign;
    NvBool bEncryptionEnabled;
    NvBool bErrorInjectionEnabled;

    // Pointers to shared memory
    void         *pCommandQueue;
    void         *pStatusQueue;
    void         *pWorkArea;
    void         *pMetaData;
    GSP_MSG_QUEUE_ELEMENT *pCmdQueueElement;

    // RPC buffer pointer within the element
    GspRpcMessageHeader *pRpcMsgBuf;

    // MSGQ handle
    int          hQueue;

    // Encryption context
    void         *pEncryptCtx;
} MESSAGE_QUEUE_INFO;

// Queue collection — holds shared memory and PTEs
#define RPC_TASK_RM_QUEUE_IDX   0
#define RPC_QUEUE_COUNT         1
typedef struct MESSAGE_QUEUE_COLLECTION {
    MEMORY_DESCRIPTOR *pSharedMemDesc;
    NvU32  pageTableSize;
    NvU32  pageTableEntryCount;
    NvU64  sharedMemPA;      // physical address of first page
    void  *pVa;              // kernel virtual address of shared memory
    MESSAGE_QUEUE_INFO rpcQueues[RPC_QUEUE_COUNT];
} MESSAGE_QUEUE_COLLECTION;

// MSGQ handle type
typedef int RM_MSGQ_HANDLE;

// msgq library functions (implemented in GSPQueue.cpp)
extern "C" {
int  msgqInit(RM_MSGQ_HANDLE *hQueue, void *pMetaData);
int  msgqTxCreate(RM_MSGQ_HANDLE hQueue, void *buf, NvU32 size,
                   NvU32 elMin, NvU32 hdrAlign, NvU32 elAlign, NvU32 flags);
int  msgqTxGetWriteBuffer(RM_MSGQ_HANDLE hQueue, NvU32 index, void **ppBuf);
int  msgqTxSubmitBuffers(RM_MSGQ_HANDLE hQueue, NvU32 nElements);
int  msgqRxLink(RM_MSGQ_HANDLE hQueue, const void *buf, NvU32 size, NvU32 elMin);
int  msgqRxGetReadBuffer(RM_MSGQ_HANDLE hQueue, NvU32 index, const void **ppBuf);
int  msgqRxMarkConsumed(RM_MSGQ_HANDLE hQueue, NvU32 nElements);
int  msgqGetMetaSize(void);
}

#define MSGQ_FLAGS_SWAP_RX          (1 << 0)

// GSP queue helper functions (implemented in wrappers)
static inline GspRpcMessageHeader*
gspMsgQueueGetRpcMessageHeader(const MESSAGE_QUEUE_INFO *pMQI,
                                const GSP_MSG_QUEUE_ELEMENT *pElement)
{
    NvUPtr base = (NvUPtr)&pElement->payload[0];
    if (pMQI->bEncryptionEnabled)
        base += sizeof(GSP_MSG_QUEUE_ENCRYPTION_TAG);
    return (GspRpcMessageHeader*)base;
}

static inline NvU32 gspMsgQueueGetRpcMessageLength(const MESSAGE_QUEUE_INFO *pMQI,
                                                     const GSP_MSG_QUEUE_ELEMENT *pElement)
{
    return pMQI->queueElementHdrSize;
}

static inline NvU32 gspMsgQueueBytesToElements(NvU32 bytes, NvU32 elMin)
{
    return NV_DIV_AND_CEIL(bytes, elMin);
}

// ============================================================================
// 13. RM PAGE SIZE CONSTANTS
// ============================================================================

#define RM_PAGE_SIZE            4096
#define RM_PAGE_SHIFT           12
#define RM_PAGE_SIZE_128K       0x20000

// ============================================================================
// 16. HW PLATFORM CHECKS (stubs for message_queue_cpu.c)
// ============================================================================

#define IS_EMULATION(pGpu)                      NV_FALSE
#define IS_SIMULATION(pGpu)                     NV_FALSE

static inline void threadStateResetTimeout(OBJGPU *pGpu) {}

// ============================================================================
// 17. CC/ENCRYPTION STUBS (for message_queue_cpu.c)
// ============================================================================

static inline NvBool confComputeForceUnprotAlloc(OBJGPU *pGpu) { return NV_FALSE; }
static inline void    confComputeSetErrorState(OBJGPU *pGpu, void *pCC) {}
static inline NV_STATUS gspMsgQueueCCEncrypt(void *ctx, MESSAGE_QUEUE_INFO *pMQI,
                                              GSP_MSG_QUEUE_ELEMENT *pElement, NvU32 len)
{
    return NV_OK;
}
static inline NV_STATUS gspMsgQueueCCDecrypt(void *ctx, MESSAGE_QUEUE_INFO *pMQI,
                                              GSP_MSG_QUEUE_ELEMENT *pElement, NvU32 len)
{
    return NV_OK;
}

// ============================================================================
// 18. MESSAGE QUEUE HELPERS (for message_queue_cpu.c)
// ============================================================================

static inline GSP_MSG_QUEUE_ENCRYPTION_TAG*
gspMsgQueueGetEncryptionTag(GSP_MSG_QUEUE_ELEMENT *pElement)
{
    return (GSP_MSG_QUEUE_ENCRYPTION_TAG*)&pElement->payload[0];
}

// Simple 32-bit checksum (sum of all dwords)
static inline NvU32 _checkSum32(const void *data, NvU32 len)
{
    NvU32 sum = 0;
    const NvU32 *p = (const NvU32*)data;
    for (NvU32 i = 0; i < len / 4; i++) sum += p[i];
    return sum;
}

#endif /* os_compat_h */
