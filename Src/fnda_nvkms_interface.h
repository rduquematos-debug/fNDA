// fnda_nvkms_interface.h — NVKMS OS interface + minimal types for IOKit
#ifndef fnda_nvkms_interface_h
#define fnda_nvkms_interface_h

#include <libkern/libkern.h>
#include <IOKit/IOReturn.h>
#include <stdarg.h>
#include <kern/clock.h>

// -- NVIDIA type system (minimal) —
// Guarded: os_compat.h provides the same types via GA104Device.hpp
#ifndef NvU32
typedef uint8_t  NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef int8_t   NvS8;
typedef int16_t  NvS16;
typedef int32_t  NvS32;
typedef int64_t  NvS64;
#endif
#ifndef NV_NVBOOL_DEFINED
#define NV_NVBOOL_DEFINED
typedef int      NvBool;
#endif
#ifndef NV_TRUE
#define NV_TRUE   1
#define NV_FALSE  0
#endif

// -- Forward decls for NVKMS types --
typedef struct nvkms_per_open    nvkms_per_open_handle_t;
typedef struct NvKmsKapiDevice   NvKmsKapiDevice;
struct NvKmsKapiFunctionsTable;

typedef struct {
    NvU32 vendor_id;
    NvU32 device_id;
    NvU32 sub_vendor_id;
    NvU32 sub_device_id;
    NvU32 gpu_id;
} nv_gpu_info_t;

// -- NVKMS OS Interface (declarations matching nvidia-modeset-os-interface.h) --
enum NvKmsSyncPtOp {
    NVKMS_SYNCPT_OP_ALLOC,
    NVKMS_SYNCPT_OP_PUT,
    NVKMS_SYNCPT_OP_FD_TO_ID_AND_THRESH,
    NVKMS_SYNCPT_OP_ID_AND_THRESH_TO_FD,
    NVKMS_SYNCPT_OP_READ_MINVAL,
};
typedef struct { struct { const char *syncpt_name; NvU32 id; } alloc;
                 struct { NvU32 id; } put;
                 struct { NvS32 fd; NvU32 id; NvU32 thresh; } fd_to_id_and_thresh;
                 struct { NvU32 id; NvU32 thresh; NvS32 fd; } id_and_thresh_to_fd;
                 struct { NvU32 id; NvU32 minval; } read_minval;
               } NvKmsSyncPtOpParams;

enum NvKmsFrlRateForce { NVKMS_FRL_RATE_FORCE_NONE, NVKMS_FRL_RATE_FORCE_MAX, NVKMS_FRL_RATE_FORCE_MAX_DSC };
enum NvKmsFailAllocCoreChannelMethod { NVKMS_FAIL_ALLOC_CORE_CHANNEL_RM_SETUP_CORE_CHANNEL = 0,
                                       NVKMS_FAIL_ALLOC_CORE_CHANNEL_RESTORE_CONSOLE = 1,
                                       NVKMS_FAIL_ALLOC_CORE_CHANNEL_NO_CLASS = 2 };

// -- Functions implemented in fnda_nvkms_interface.cpp --
extern "C" {

void   nvkms_call_rm(void *ops);
struct GA104Device;
void   fnda_nvkms_register_device(struct GA104Device *dev);
struct GA104Device* fnda_nvkms_get_device(void);

// RM callbacks (implemented in fnda_nvkms_interface.cpp,
// call GA104Device::sendGspRpc*() via gRMDevice global)
extern IOReturn fnda_nvkms_do_rm_alloc(uint32_t hClient, uint32_t hParent,
    uint32_t hObject, uint32_t hClass, void *pParams, uint32_t *status);
extern IOReturn fnda_nvkms_do_rm_control(uint32_t hClient, uint32_t hObject,
    uint32_t cmd, void *pParams, uint32_t paramsSize, uint32_t *status);
extern IOReturn fnda_nvkms_do_rm_free(uint32_t hClient, uint32_t hParent, uint32_t hObject);
void*  nvkms_alloc(size_t size, NvBool zero);
void   nvkms_free(void *ptr, size_t size);
void*  nvkms_memset(void *ptr, NvU8 c, size_t size);
void*  nvkms_memcpy(void *dest, const void *src, size_t n);
void*  nvkms_memmove(void *dest, const void *src, size_t n);
int    nvkms_memcmp(const void *s1, const void *s2, size_t n);
size_t nvkms_strlen(const char *s);
int    nvkms_strcmp(const char *s1, const char *s2);
char*  nvkms_strncpy(char *dest, const char *src, size_t n);
void   nvkms_usleep(NvU64 usec);
NvU64  nvkms_get_usec(void);
int    nvkms_copyin(void *kptr, NvU64 uaddr, size_t n);
int    nvkms_copyout(NvU64 uaddr, const void *kptr, size_t n);
void   nvkms_yield(void);
void   nvkms_dump_stack(void);
NvBool nvkms_syncpt_op(enum NvKmsSyncPtOp op, NvKmsSyncPtOpParams *params);

int    nvkms_snprintf(char *str, size_t size, const char *format, ...);
int    nvkms_vsnprintf(char *str, size_t size, const char *format, va_list ap);

#define NVKMS_LOG_LEVEL_INFO  0
#define NVKMS_LOG_LEVEL_WARN  1
#define NVKMS_LOG_LEVEL_ERROR 2
void nvkms_log(int level, const char *gpuPrefix, const char *msg);

struct nvkms_ref_ptr;
struct nvkms_ref_ptr* nvkms_alloc_ref_ptr(void *ptr);
void  nvkms_free_ref_ptr(struct nvkms_ref_ptr *ref_ptr);
void  nvkms_inc_ref(struct nvkms_ref_ptr *ref_ptr);
void* nvkms_dec_ref(struct nvkms_ref_ptr *ref_ptr);

typedef void nvkms_timer_proc_t(void *dataPtr, NvU32 dataU32);
typedef struct nvkms_timer_t nvkms_timer_handle_t;
nvkms_timer_handle_t* nvkms_alloc_timer(nvkms_timer_proc_t *proc, void *dataPtr, NvU32 dataU32, NvU64 usec);
NvBool nvkms_alloc_timer_with_ref_ptr(nvkms_timer_proc_t *proc, struct nvkms_ref_ptr *ref_ptr, NvU32 dataU32, NvU64 usec);
void   nvkms_free_timer(nvkms_timer_handle_t *handle);

void nvkms_event_queue_changed(nvkms_per_open_handle_t *pOpenKernel, NvBool eventsAvailable);
void* nvkms_get_per_open_data(int fd);
NvBool nvkms_open_gpu(NvU32 gpuId, NvBool reset_aware);
void   nvkms_close_gpu(NvU32 gpuId, NvBool reset_aware);
NvU32  nvkms_enumerate_gpus(nv_gpu_info_t *gpu_info);
NvBool nvkms_allow_write_combining(void);
NvBool nvkms_kernel_supports_syncpts(void);
NvBool nvkms_fd_is_nvidia_chardev(int fd);

struct nvkms_per_open* nvkms_open_from_kapi(NvKmsKapiDevice *device);
void  nvkms_close_from_kapi(struct nvkms_per_open *popen);
NvBool nvkms_ioctl_from_kapi(struct nvkms_per_open *popen, NvU32 cmd, void *params, const size_t params_size);
NvBool nvkms_ioctl_from_kapi_try_pmlock(struct nvkms_per_open *popen, NvU32 cmd, void *params, const size_t params_size);

typedef struct nvkms_sema_t nvkms_sema_handle_t;
nvkms_sema_handle_t* nvkms_sema_alloc(void);
void nvkms_sema_free(nvkms_sema_handle_t *sema);
void nvkms_sema_down(nvkms_sema_handle_t *sema);
void nvkms_sema_up(nvkms_sema_handle_t *sema);

struct nvkms_backlight_device;
struct nvkms_backlight_device* nvkms_register_backlight(NvU32 gpu_id, NvU32 display_id, void *drv_priv, NvU32 current_brightness);
void nvkms_unregister_backlight(struct nvkms_backlight_device *nvkms_bd);

// -- Entry points from nvkms.h --
enum NvKmsClientType { NVKMS_CLIENT_USER_SPACE, NVKMS_CLIENT_KERNEL_SPACE };
struct NvKmsPerOpenDev;

NvBool nvKmsIoctl(void *pOpenVoid, NvU32 cmd, NvU64 paramsAddress, size_t paramSize);
void   nvKmsClose(void *pOpenVoid);
void*  nvKmsOpen(NvU32 pid, enum NvKmsClientType clientType, nvkms_per_open_handle_t *pOpenKernel);
NvBool nvKmsModuleLoad(void);
void   nvKmsModuleUnload(void);
void   nvKmsSuspend(NvU32 gpuId);
void   nvKmsResume(NvU32 gpuId);
NvBool nvKmsKapiGetFunctionsTableInternal(struct NvKmsKapiFunctionsTable *funcsTable);

} // extern "C"

#endif /* fnda_nvkms_interface_h */
