#ifndef GA104FBProvider_hpp
#define GA104FBProvider_hpp

#include <IOKit/IOService.h>

class GA104Device;

class GA104FBProvider : public IOService
{
    OSDeclareDefaultStructors(GA104FBProvider)

public:
    bool init(GA104Device *device);
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setGPUPolicy(OSDictionary *policy);

    GA104Device *getDevice() const { return fDevice; }

protected:
    GA104Device   *fDevice;
};

#endif
