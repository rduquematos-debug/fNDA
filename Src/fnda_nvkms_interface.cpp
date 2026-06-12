// fnda_nvkms_interface.cpp — NVKMS OS interface implementation over IOKit
#include "fnda_nvkms_interface.h"
#include "GA104Device.hpp"
#include "GSPProtocol.hpp"
#include <IOKit/IOLib.h>
#include <string.h>
#include <kern/thread_call.h>

// Global pointer to active GA104Device
static GA104Device *gRMDevice = nullptr;

void fnda_nvkms_register_device(GA104Device *dev) {
    gRMDevice = dev;
    IOLog("GA104: registered as NVKMS RM device\n");
}

GA104Device* fnda_nvkms_get_device(void) {
    return gRMDevice;
}

#pragma mark - Memory

void* nvkms_alloc(size_t size, NvBool zero) {
    void *p = (zero ? IOMallocAligned(size, 16) : IOMalloc(size));
    if (p && zero) bzero(p, size);
    return p;
}

void nvkms_free(void *ptr, size_t size) {
    if (ptr) IOFree(ptr, size);
}

void* nvkms_memset(void *ptr, NvU8 c, size_t size) { memset(ptr, c, size); return ptr; }
void* nvkms_memcpy(void *dest, const void *src, size_t n) { return memcpy(dest, src, n); }
void* nvkms_memmove(void *dest, const void *src, size_t n) { return memmove(dest, src, n); }
int   nvkms_memcmp(const void *s1, const void *s2, size_t n) { return memcmp(s1, s2, n); }
size_t nvkms_strlen(const char *s) { return strlen(s); }
int   nvkms_strcmp(const char *s1, const char *s2) { return strcmp(s1, s2); }
char* nvkms_strncpy(char *dest, const char *src, size_t n) { return strncpy(dest, src, n); }

#pragma mark - Time

void nvkms_usleep(NvU64 usec) { IODelay((uint32_t)(usec > 1000000 ? 1000000 : usec)); }
NvU64 nvkms_get_usec(void) {
    uint64_t t;
    clock_get_uptime(&t);
    return t * 1000000 / NSEC_PER_SEC;
}

#pragma mark - Copy in/out (no userspace in kext — stubs)

int nvkms_copyin(void *kptr, NvU64 uaddr, size_t n) { memcpy(kptr, (void*)(uintptr_t)uaddr, n); return 0; }
int nvkms_copyout(NvU64 uaddr, const void *kptr, size_t n) { memcpy((void*)(uintptr_t)uaddr, kptr, n); return 0; }

#pragma mark - Misc

void nvkms_yield(void) {}
void nvkms_dump_stack(void) { IOLog("nvkms: stack dump requested\n"); }
NvBool nvkms_syncpt_op(enum NvKmsSyncPtOp op, NvKmsSyncPtOpParams *params) { return NV_FALSE; }

#pragma mark - Logging

int nvkms_snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int r = vsnprintf(str, size, format, ap);
    va_end(ap); return r;
}

int nvkms_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    return vsnprintf(str, size, format, ap);
}

void nvkms_log(int level, const char *gpuPrefix, const char *msg) {
    switch (level) {
        case NVKMS_LOG_LEVEL_ERROR: IOLog("NVKMS ERR: %s %s\n", gpuPrefix ? gpuPrefix : "", msg); break;
        case NVKMS_LOG_LEVEL_WARN:  IOLog("NVKMS WARN: %s %s\n", gpuPrefix ? gpuPrefix : "", msg); break;
        default:                    IOLog("NVKMS: %s %s\n", gpuPrefix ? gpuPrefix : "", msg); break;
    }
}

#pragma mark - Ref ptr

struct nvkms_ref_ptr { void *ptr; int refcnt; };

struct nvkms_ref_ptr* nvkms_alloc_ref_ptr(void *ptr) {
    struct nvkms_ref_ptr *r = (struct nvkms_ref_ptr*)nvkms_alloc(sizeof(struct nvkms_ref_ptr), NV_TRUE);
    if (r) { r->ptr = ptr; r->refcnt = 1; }
    return r;
}

void nvkms_free_ref_ptr(struct nvkms_ref_ptr *ref_ptr) {
    if (!ref_ptr) return;
    ref_ptr->ptr = NULL;
    if (--ref_ptr->refcnt <= 0) nvkms_free(ref_ptr, sizeof(struct nvkms_ref_ptr));
}

void nvkms_inc_ref(struct nvkms_ref_ptr *ref_ptr) {
    if (ref_ptr) OSIncrementAtomic((volatile SInt32*)&ref_ptr->refcnt);
}

void* nvkms_dec_ref(struct nvkms_ref_ptr *ref_ptr) {
    if (!ref_ptr) return NULL;
    void *ptr = ref_ptr->ptr;
    if (OSDecrementAtomic((volatile SInt32*)&ref_ptr->refcnt) <= 1) {
        if (!ref_ptr->ptr) { nvkms_free(ref_ptr, sizeof(struct nvkms_ref_ptr)); return NULL; }
    }
    return ptr;
}

#pragma mark - Timer

struct nvkms_timer_t {
    thread_call_t call;
    nvkms_timer_proc_t *proc;
    void *dataPtr;
    NvU32 dataU32;
    struct nvkms_ref_ptr *ref_ptr;
    bool with_ref;
};

static void timer_callback(thread_call_param_t p0, thread_call_param_t p1) {
    nvkms_timer_handle_t *t = (nvkms_timer_handle_t*)p0;
    if (!t) return;
    if (t->with_ref && t->ref_ptr) {
        void *ptr = nvkms_dec_ref(t->ref_ptr);
        if (ptr) t->proc(ptr, t->dataU32);
    } else {
        t->proc(t->dataPtr, t->dataU32);
    }
    if (t->with_ref) nvkms_free(t, sizeof(nvkms_timer_handle_t));
}

nvkms_timer_handle_t* nvkms_alloc_timer(nvkms_timer_proc_t *proc, void *dataPtr, NvU32 dataU32, NvU64 usec) {
    nvkms_timer_handle_t *t = (nvkms_timer_handle_t*)nvkms_alloc(sizeof(nvkms_timer_handle_t), NV_TRUE);
    if (!t) return NULL;
    t->proc = proc; t->dataPtr = dataPtr; t->dataU32 = dataU32; t->with_ref = false;
    t->call = thread_call_allocate(timer_callback, (thread_call_param_t)t);
    if (!t->call) { nvkms_free(t, sizeof(nvkms_timer_handle_t)); return NULL; }
    if (usec > 0) {
        uint64_t deadline, interval = usec * NSEC_PER_USEC;
        clock_absolutetime_interval_to_deadline(interval, &deadline);
        thread_call_enter_delayed(t->call, deadline);
    } else {
        thread_call_enter(t->call);
    }
    return t;
}

NvBool nvkms_alloc_timer_with_ref_ptr(nvkms_timer_proc_t *proc, struct nvkms_ref_ptr *ref_ptr, NvU32 dataU32, NvU64 usec) {
    nvkms_timer_handle_t *t = (nvkms_timer_handle_t*)nvkms_alloc(sizeof(nvkms_timer_handle_t), NV_TRUE);
    if (!t) return NV_FALSE;
    t->proc = proc; t->ref_ptr = ref_ptr; t->dataU32 = dataU32; t->with_ref = true;
    if (ref_ptr) nvkms_inc_ref(ref_ptr);
    t->call = thread_call_allocate(timer_callback, (thread_call_param_t)t);
    if (!t->call) { nvkms_free(t, sizeof(nvkms_timer_handle_t)); return NV_FALSE; }
    if (usec > 0) {
        uint64_t deadline, interval = usec * NSEC_PER_USEC;
        clock_absolutetime_interval_to_deadline(interval, &deadline);
        thread_call_enter_delayed(t->call, deadline);
    } else {
        thread_call_enter(t->call);
    }
    return NV_TRUE;
}

void nvkms_free_timer(nvkms_timer_handle_t *handle) {
    if (!handle) return;
    if (handle->call) thread_call_cancel(handle->call);
    nvkms_free(handle, sizeof(nvkms_timer_handle_t));
}

#pragma mark - Event / GPU enumeration

void nvkms_event_queue_changed(nvkms_per_open_handle_t *pOpenKernel, NvBool eventsAvailable) {}
void* nvkms_get_per_open_data(int fd) { return NULL; }
NvBool nvkms_open_gpu(NvU32 gpuId, NvBool reset_aware) { return NV_TRUE; }
void nvkms_close_gpu(NvU32 gpuId, NvBool reset_aware) {}
NvU32 nvkms_enumerate_gpus(nv_gpu_info_t *gpu_info) { return 0; }
NvBool nvkms_allow_write_combining(void) { return NV_TRUE; }
NvBool nvkms_kernel_supports_syncpts(void) { return NV_FALSE; }
NvBool nvkms_fd_is_nvidia_chardev(int fd) { return NV_FALSE; }

#pragma mark - KAPI bridge

struct nvkms_per_open* nvkms_open_from_kapi(NvKmsKapiDevice *device) { return NULL; }
void nvkms_close_from_kapi(struct nvkms_per_open *popen) {}
NvBool nvkms_ioctl_from_kapi(struct nvkms_per_open *popen, NvU32 cmd, void *params, size_t params_size) { return NV_FALSE; }
NvBool nvkms_ioctl_from_kapi_try_pmlock(struct nvkms_per_open *popen, NvU32 cmd, void *params, size_t params_size) { return NV_FALSE; }

#pragma mark - Semaphore

struct nvkms_sema_t { int dummy; };
nvkms_sema_handle_t* nvkms_sema_alloc(void) { return (nvkms_sema_handle_t*)nvkms_alloc(sizeof(nvkms_sema_handle_t), NV_TRUE); }
void nvkms_sema_free(nvkms_sema_handle_t *sema) { if (sema) nvkms_free(sema, sizeof(nvkms_sema_handle_t)); }
void nvkms_sema_down(nvkms_sema_handle_t *sema) {}
void nvkms_sema_up(nvkms_sema_handle_t *sema) {}

#pragma mark - Backlight

struct nvkms_backlight_device* nvkms_register_backlight(NvU32 gpu_id, NvU32 display_id, void *drv_priv, NvU32 current_brightness) { return NULL; }
void nvkms_unregister_backlight(struct nvkms_backlight_device *nvkms_bd) {}

#pragma mark - RM communication bridge

// Minimal RM ops structures (matching nvidia_kernel_rmapi_ops_t layout)
#define NV04_ALLOC              0x00008004
#define NV04_CONTROL            0x0000800E
// RM API Ioctl numbers (NV04_MAP_MEMORY renamed to avoid conflict with GA104Regs.h)
#define NV01_FREE               0x00000001
#define NV01_ALLOC_MEMORY       0x00000002
#define NV04_VID_HEAP_CONTROL   0x0000801B
#define RMAPI_MAP_MEMORY        0x00008010
#define NV04_UNMAP_MEMORY       0x00008011
#define NV04_MAP_MEMORY_DMA     0x00008012
#define NV04_UNMAP_MEMORY_DMA   0x00008017
#define NV04_DUP_OBJECT         0x0000800F
#define NV04_ALLOC_CONTEXT_DMA  0x0000800D
#define NV04_BIND_CONTEXT_DMA   0x00008013
#define NV04_SHARE              0x00008015
#define NV04_ADD_VBLANK_CALLBACK 0x00008016

typedef struct {
    NvU32 hRoot, hObjectParent, hObjectNew, hClass;
    NvU64 pAllocParms;
    NvU32 status, paramsSize;
    NvU32 flags;
} RMAllocParams;

typedef struct {
    NvU32 hClient, hObject, cmd;
    NvU64 params;
    NvU32 paramsSize, status, flags;
} RMControlParams;

typedef struct {
    NvU32 hRoot, hObjectParent, hObjectOld;
    NvU32 status, flags;
} RMFreeParams;

typedef struct {
    NvU32 op;
    union {
        RMAllocParams    alloc;
        RMControlParams  control;
        RMFreeParams     free;
    } params;
} RMOps;

// Forward declaration: the driver will call this
extern IOReturn fnda_nvkms_do_rm_alloc(NvU32 hClient, NvU32 hParent,
    NvU32 hObject, NvU32 hClass, void *pParams, NvU32 *status);
extern IOReturn fnda_nvkms_do_rm_control(NvU32 hClient, NvU32 hObject,
    NvU32 cmd, void *pParams, NvU32 paramsSize, NvU32 *status);
extern IOReturn fnda_nvkms_do_rm_free(NvU32 hClient, NvU32 hParent, NvU32 hObject);

void nvkms_call_rm(void *ops) {
    RMOps *rmops = (RMOps *)ops;
    if (!rmops) return;

    switch (rmops->op) {
        case NV04_ALLOC: {
            RMAllocParams *a = &rmops->params.alloc;
            NvU32 st = 0;
            fnda_nvkms_do_rm_alloc(a->hRoot, a->hObjectParent, a->hObjectNew,
                                   a->hClass, (void*)(uintptr_t)a->pAllocParms, &st);
            a->status = st;
            break;
        }
        case NV04_CONTROL: {
            RMControlParams *c = &rmops->params.control;
            NvU32 st = 0;
            fnda_nvkms_do_rm_control(c->hClient, c->hObject, c->cmd,
                                     (void*)(uintptr_t)c->params, c->paramsSize, &st);
            c->status = st;
            break;
        }
        case NV01_FREE: {
            RMFreeParams *f = &rmops->params.free;
            NvU32 st = 0;
            fnda_nvkms_do_rm_free(f->hRoot, f->hObjectParent, f->hObjectOld);
            f->status = st;
            break;
        }
        default:
            IOLog("nvkms_call_rm: unknown op 0x%x\n", (unsigned)rmops->op);
            break;
    }
}

#pragma mark - RM API bridge implementation (over GA104Device GSP RPC)

IOReturn fnda_nvkms_do_rm_alloc(uint32_t hClient, uint32_t hParent,
    uint32_t hObject, uint32_t hClass, void *pParams, uint32_t *status) {
    GA104Device *dev = gRMDevice;
    if (!dev || !dev->getGSPProtocol()) {
        IOLog("nvkms_rm_alloc: no device\n");
        if (status) *status = 0xFFFFFFFF;
        return kIOReturnNotReady;
    }
    GSPModesetParams dummy; bzero(&dummy, sizeof(dummy));
    IOReturn ret = dev->sendGspRpcAllocHandle(hClient, hParent, hObject, hClass,
                                              pParams, status);
    return ret;
}

IOReturn fnda_nvkms_do_rm_control(NvU32 hClient, NvU32 hObject,
    NvU32 cmd, void *pParams, NvU32 paramsSize, NvU32 *status) {
    GA104Device *dev = gRMDevice;
    if (!dev || !dev->getGSPProtocol()) {
        IOLog("nvkms_rm_control: no device\n");
        if (status) *status = 0xFFFFFFFF;
        return kIOReturnNotReady;
    }
    IOReturn ret = dev->sendGspRpcControl(hClient, hObject, cmd,
                                          pParams, paramsSize, status);
    return ret;
}

IOReturn fnda_nvkms_do_rm_free(NvU32 hClient, NvU32 hParent, NvU32 hObject) {
    GA104Device *dev = gRMDevice;
    if (!dev) return kIOReturnNotReady;
    IOReturn ret = dev->sendGspRpcFree(hClient, hParent, hObject);
    return ret;
}

#pragma mark - Entry points (stubs for now — GA104Device will call these)

void* nvKmsOpen(NvU32 pid, enum NvKmsClientType clientType, nvkms_per_open_handle_t *pOpenKernel) {
    IOLog("nvKmsOpen: pid=%u type=%d\n", (unsigned)pid, (int)clientType);
    return (void*)(uintptr_t)0x1; // non-NULL placeholder
}

void nvKmsClose(void *pOpenVoid) {}

NvBool nvKmsIoctl(void *pOpenVoid, NvU32 cmd, NvU64 paramsAddress, size_t paramSize) {
    return NV_FALSE;
}

NvBool nvKmsModuleLoad(void) { return NV_TRUE; }
void nvKmsModuleUnload(void) {}
void nvKmsSuspend(NvU32 gpuId) {}
void nvKmsResume(NvU32 gpuId) {}

NvBool nvKmsKapiGetFunctionsTableInternal(struct NvKmsKapiFunctionsTable *funcsTable) {
    return NV_FALSE;
}
