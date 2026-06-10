#ifndef GA104UserClient_hpp
#define GA104UserClient_hpp

#include <IOKit/IOUserClient.h>
#include "GA104Device.hpp"

enum GA104Method {
    kGA104MethodGetGSPStatus  = 0,
    kGA104MethodLoadFirmware  = 1,
    kGA104MethodBootGSP       = 2,
    kGA104MethodGetBARInfo    = 3,
    kGA104MethodReadReg       = 4,
    kGA104MethodWriteReg      = 5,
    kGA104MethodGetDeviceInfo = 6,
    kGA104MethodFWAppendChunk = 7,
    kGA104MethodFinalize = 8,
    kGA104MethodLoadBootloader = 9,
    kGA104MethodBLBufferAlloc = 10,
    kGA104MethodBootMainFirmware = 11,
    kGA104MethodInitDisplay = 12,
    kGA104MethodSetCOTPayload = 13,
    kGA104MethodBootSEC2 = 14,
    kGA104MethodFillFramebuffer = 15,
    kGA104MethodFlipToTriangle = 16,
    kGA104MethodFlipToFramebuffer = 17,
    kGA104MethodReadCSRs = 18,
    kGA104MethodWriteVRAM   = 19,   // write bytes to VRAM offset
    kGA104MethodReadVRAM    = 20,   // read bytes from VRAM offset
    kGA104MethodReadCoreGET = 21,   // read NV_PDISP_CORE_GET
    kGA104MethodWriteCorePUT = 22,  // write NV_PDISP_CORE_PUT (dword offset)
    kGA104MethodGetCorePbAddr = 23, // get core pushbuffer VRAM offset
    kGA104MethodCount
};

class GA104UserClient : public IOUserClient
{
    OSDeclareDefaultStructors(GA104UserClient)

public:
    virtual bool init(OSDictionary *dict) override;
    virtual void free() override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;

    virtual IOReturn clientClose(void) override;
    virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
                                         IOMemoryDescriptor **memory) override;
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
                                    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) override;

protected:
    GA104Device *fDevice;

    static const IOExternalMethodDispatch sMethods[kGA104MethodCount];

    static IOReturn sGetGSPStatus(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sLoadFirmware(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sBootGSP(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sGetBARInfo(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sReadReg(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sWriteReg(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sGetDeviceInfo(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sFWAppendChunk(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sFinalize(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sLoadBootloader(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sBLBufferAlloc(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sBootMainFirmware(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sInitDisplay(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sSetCOTPayload(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sBootSEC2(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sFillFramebuffer(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sFlipToTriangle(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sFlipToFramebuffer(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sReadCSRs(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sWriteVRAM(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sReadVRAM(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sReadCoreGET(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sWriteCorePUT(OSObject *target, void *reference, IOExternalMethodArguments *args);
    static IOReturn sGetCorePbAddr(OSObject *target, void *reference, IOExternalMethodArguments *args);
};

#endif /* GA104UserClient_hpp */
