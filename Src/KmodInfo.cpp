#include <mach/kmod.h>
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>

extern kern_return_t ga104_start(struct kmod_info *ki, void *data);
extern kern_return_t ga104_stop(struct kmod_info *ki, void *data);

KMOD_EXPLICIT_DECL(com.nvidia.driver.GA104, "1.0.0", ga104_start, ga104_stop)

kern_return_t ga104_start(struct kmod_info *ki, void *data)
{
    IOLog("GA104: kmod_start (kext loaded!)\n");
    return KMOD_RETURN_SUCCESS;
}

kern_return_t ga104_stop(struct kmod_info *ki, void *data)
{
    IOLog("GA104: kmod_stop (kext unloaded)\n");
    return KMOD_RETURN_SUCCESS;
}
