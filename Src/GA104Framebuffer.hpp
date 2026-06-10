#ifndef GA104Framebuffer_hpp
#define GA104Framebuffer_hpp

#include <IOKit/graphics/IOFramebuffer.h>

class GA104Device;

class GA104Framebuffer : public IOFramebuffer
{
    OSDeclareDefaultStructors(GA104Framebuffer)

public:
    bool init(GA104Device *device, IOPhysicalAddress fbPhys, IOByteCount fbSize,
              UInt32 width, UInt32 height);
    virtual void free() APPLE_KEXT_OVERRIDE;

    // IOFramebuffer pure virtuals
    virtual IODeviceMemory * getApertureRange(IOPixelAperture aperture) APPLE_KEXT_OVERRIDE;
    virtual const char * getPixelFormats() APPLE_KEXT_OVERRIDE;
    virtual IOItemCount getDisplayModeCount() APPLE_KEXT_OVERRIDE;
    virtual IOReturn getDisplayModes(IODisplayModeID *allDisplayModes) APPLE_KEXT_OVERRIDE;
    virtual IOReturn getInformationForDisplayMode(IODisplayModeID displayMode,
                                                  IODisplayModeInformation *info) APPLE_KEXT_OVERRIDE;
    virtual UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode,
                                                  IOIndex depth) APPLE_KEXT_OVERRIDE;
    virtual IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                          IOPixelAperture aperture,
                                          IOPixelInformation *pixelInfo) APPLE_KEXT_OVERRIDE;
    virtual IOReturn getCurrentDisplayMode(IODisplayModeID *displayMode,
                                            IOIndex *depth) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth) APPLE_KEXT_OVERRIDE;

    // Connection/attribute overrides
    virtual IOItemCount getConnectionCount() APPLE_KEXT_OVERRIDE;
    virtual IOReturn getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t *value) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                uintptr_t value) APPLE_KEXT_OVERRIDE;
    virtual IOReturn connectFlags(IOIndex connectIndex, IODisplayModeID displayMode,
                                   IOOptionBits *flags) APPLE_KEXT_OVERRIDE;
    virtual IOReturn getAttribute(IOSelect attribute, uintptr_t *value) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setAttribute(IOSelect attribute, uintptr_t value) APPLE_KEXT_OVERRIDE;

    // VRAM / console
    virtual IODeviceMemory * getVRAMRange() APPLE_KEXT_OVERRIDE;
    virtual bool isConsoleDevice() APPLE_KEXT_OVERRIDE;

protected:
    GA104Device      *fDevice;
    IOPhysicalAddress fFBPhys;
    IOByteCount       fFBSize;
    UInt32            fWidth;
    UInt32            fHeight;
    UInt32            fRefreshRate;
    IOIndex           fCurrentDepth;
    IODisplayModeID   fCurrentModeID;
};

#endif
