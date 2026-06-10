#include "GA104Framebuffer.hpp"
#include "GA104Device.hpp"
#include <IOKit/IOLib.h>
#include <string.h>

#define super IOFramebuffer
OSDefineMetaClassAndStructors(GA104Framebuffer, IOFramebuffer)

static const char *kGA104PixelFormat = "--------RRRRRRRRGGGGGGGGBBBBBBBB";

#define ENCODE_MODE_ID(w, h) ((IODisplayModeID)(0x80000000 | ((w) << 16) | (h)))

bool GA104Framebuffer::init(GA104Device *device, IOPhysicalAddress fbPhys,
                             IOByteCount fbSize, UInt32 width, UInt32 height)
{
    if (!super::init()) return false;

    fDevice = device;
    fFBPhys = fbPhys;
    fFBSize = fbSize;
    fWidth = width;
    fHeight = height;
    fRefreshRate = (60 << 16);
    fCurrentDepth = 0;
    fCurrentModeID = ENCODE_MODE_ID(width, height);

    return true;
}

void GA104Framebuffer::free()
{
    fDevice = nullptr;
    super::free();
}

IODeviceMemory * GA104Framebuffer::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) return nullptr;
    return IODeviceMemory::withRange(fFBPhys, fFBSize);
}

IODeviceMemory * GA104Framebuffer::getVRAMRange()
{
    if (!fDevice) return nullptr;
    return IODeviceMemory::withRange(fDevice->getBAR1Phys(), fDevice->getBAR1Size());
}

const char * GA104Framebuffer::getPixelFormats()
{
    return kGA104PixelFormat;
}

IOItemCount GA104Framebuffer::getDisplayModeCount()
{
    return 1;
}

IOReturn GA104Framebuffer::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    allDisplayModes[0] = fCurrentModeID;
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::getInformationForDisplayMode(
    IODisplayModeID displayMode, IODisplayModeInformation *info)
{
    if (!info) return kIOReturnBadArgument;
    if (displayMode != fCurrentModeID) return kIOReturnUnsupported;

    info->nominalWidth = fWidth;
    info->nominalHeight = fHeight;
    info->refreshRate = fRefreshRate;
    info->maxDepthIndex = 1;
    info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
    info->imageWidth = 0;
    info->imageHeight = 0;
    bzero(info->reserved, sizeof(info->reserved));

    return kIOReturnSuccess;
}

UInt64 GA104Framebuffer::getPixelFormatsForDisplayMode(
    IODisplayModeID displayMode, IOIndex depth)
{
    if (displayMode != fCurrentModeID) return kIOPixelEncodingNotSupported;
    return ((UInt64)kIOBitsPerColorComponent8 << 32) | kIOPixelEncodingRGB444;
}

IOReturn GA104Framebuffer::getPixelInformation(
    IODisplayModeID displayMode, IOIndex depth,
    IOPixelAperture aperture, IOPixelInformation *pixelInfo)
{
    if (!pixelInfo) return kIOReturnBadArgument;
    if (displayMode != fCurrentModeID) return kIOReturnUnsupported;
    if (aperture != kIOFBSystemAperture) return kIOReturnUnsupported;

    pixelInfo->bytesPerRow = fWidth * 4;
    pixelInfo->bytesPerPlane = 0;
    pixelInfo->bitsPerPixel = 32;
    pixelInfo->pixelType = kIORGBDirectPixels;
    pixelInfo->componentCount = 3;
    pixelInfo->bitsPerComponent = 8;
    bzero(pixelInfo->componentMasks, sizeof(pixelInfo->componentMasks));
    pixelInfo->componentMasks[0] = 0x00FF0000;
    pixelInfo->componentMasks[1] = 0x0000FF00;
    pixelInfo->componentMasks[2] = 0x000000FF;
    strncpy(pixelInfo->pixelFormat, kGA104PixelFormat, sizeof(pixelInfo->pixelFormat));
    pixelInfo->flags = 0;
    pixelInfo->activeWidth = fWidth;
    pixelInfo->activeHeight = fHeight;
    bzero(pixelInfo->reserved, sizeof(pixelInfo->reserved));

    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (!displayMode || !depth) return kIOReturnBadArgument;
    *displayMode = fCurrentModeID;
    *depth = fCurrentDepth;
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    if (displayMode != fCurrentModeID) return kIOReturnUnsupported;
    fCurrentDepth = depth;
    return kIOReturnSuccess;
}

IOItemCount GA104Framebuffer::getConnectionCount()
{
    return 1;
}

IOReturn GA104Framebuffer::getAttributeForConnection(
    IOIndex connectIndex, IOSelect attribute, uintptr_t *value)
{
    if (connectIndex != 0) return kIOReturnNoDevice;
    if (!value) return kIOReturnBadArgument;

    switch (attribute) {
        case kConnectionFlags:
            *value = 0;
            return kIOReturnSuccess;
        default:
            return kIOReturnUnsupported;
    }
}

IOReturn GA104Framebuffer::setAttributeForConnection(
    IOIndex connectIndex, IOSelect attribute, uintptr_t value)
{
    if (connectIndex != 0) return kIOReturnNoDevice;
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::connectFlags(
    IOIndex connectIndex, IODisplayModeID displayMode, IOOptionBits *flags)
{
    if (connectIndex != 0) return kIOReturnNoDevice;
    if (!flags) return kIOReturnBadArgument;
    *flags = 0;
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::getAttribute(IOSelect attribute, uintptr_t *value)
{
    if (!value) return kIOReturnBadArgument;
    *value = 0;
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::setAttribute(IOSelect attribute, uintptr_t value)
{
    return kIOReturnSuccess;
}

bool GA104Framebuffer::isConsoleDevice()
{
    return true;
}
