#include "GA104Framebuffer.hpp"
#include "GA104Device.hpp"
#include <IOKit/IOLib.h>
#include <string.h>

#define super IOFramebuffer
OSDefineMetaClassAndStructors(GA104Framebuffer, IOFramebuffer)

static const char *kGA104PixelFormat = "--------RRRRRRRRGGGGGGGGBBBBBBBB";

#define ENCODE_MODE_ID(w, h) ((IODisplayModeID)(0x80000000 | ((w) << 16) | (h)))

static uint16_t decodeWidth(IODisplayModeID id) {
    return (id >> 16) & 0x7FFF;
}
static uint16_t decodeHeight(IODisplayModeID id) {
    return id & 0xFFFF;
}
static IODisplayModeID encodeMode(const EDIDMode &m) {
    return ENCODE_MODE_ID(m.width, m.height);
}
static int findModeByID(const EDIDMode *modes, uint32_t count, IODisplayModeID id) {
    uint16_t w = decodeWidth(id), h = decodeHeight(id);
    for (uint32_t i = 0; i < count; i++)
        if (modes[i].width == w && modes[i].height == h) return (int)i;
    return -1;
}

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
    fModeCount = 0;
    bzero(fModes, sizeof(fModes));
    fCurrentModeID = ENCODE_MODE_ID(width, height);
    return true;
}

bool GA104Framebuffer::start(IOService *provider)
{
    if (!super::start(provider)) return false;
    IOLog("GA104FB: starting\n");

    // Try to read EDID via GSP RPC (best-effort, may not be available yet)
    if (fDevice) {
        uint8_t *edid = fDevice->getEDID();
        uint32_t edidSize = fDevice->getEDIDSize();

        if (edid && edidSize >= 128 && fEDIDParser.parse(edid, edidSize)) {
            IOLog("GA104FB: EDID parsed: %s (%u modes)\n",
                  fEDIDParser.getMonitorName(), fEDIDParser.getModeCount());
        } else {
            IOLog("GA104FB: EDID not available, using fallback\n");
        }

        // Build mode list from EDID
        uint32_t edidCount = fEDIDParser.getModeCount();
        if (edidCount > 0) {
            for (uint32_t i = 0; i < edidCount && fModeCount < EDID_MAX_MODES; i++) {
                fModes[fModeCount++] = fEDIDParser.getMode(i);
            }
        }

        // Always include the default framebuffer size
        bool hasDefault = false;
        for (uint32_t i = 0; i < fModeCount; i++)
            if (fModes[i].width == fWidth && fModes[i].height == fHeight)
                { hasDefault = true; break; }
        if (!hasDefault) {
            EDIDMode def; bzero(&def, sizeof(def));
            def.width = fWidth; def.height = fHeight; def.refreshHz = 60;
            def.preferred = true;
            fModes[fModeCount++] = def;
        }

        fCurrentModeID = ENCODE_MODE_ID(fWidth, fHeight);
        setProperty("GA104FB_Modes", (uint64_t)fModeCount, 32);
        IOLog("GA104FB: %u display modes available\n", fModeCount);
    }

    registerService();
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
    return fModeCount > 0 ? (IOItemCount)fModeCount : 1;
}

IOReturn GA104Framebuffer::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (!allDisplayModes) return kIOReturnBadArgument;
    if (fModeCount == 0) {
        allDisplayModes[0] = fCurrentModeID;
        return kIOReturnSuccess;
    }
    for (uint32_t i = 0; i < fModeCount; i++)
        allDisplayModes[i] = encodeMode(fModes[i]);
    return kIOReturnSuccess;
}

IOReturn GA104Framebuffer::getInformationForDisplayMode(
    IODisplayModeID displayMode, IODisplayModeInformation *info)
{
    if (!info) return kIOReturnBadArgument;
    int idx = findModeByID(fModes, fModeCount, displayMode);
    if (idx < 0 && displayMode == fCurrentModeID) {
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
    if (idx < 0) return kIOReturnUnsupported;

    const EDIDMode &m = fModes[idx];
    info->nominalWidth = m.width;
    info->nominalHeight = m.height;
    info->refreshRate = (uint32_t)m.refreshHz << 16;
    info->maxDepthIndex = 1;
    info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag;
    if (m.preferred) info->flags |= kDisplayModeDefaultFlag;
    info->imageWidth = 0;
    info->imageHeight = 0;
    bzero(info->reserved, sizeof(info->reserved));
    return kIOReturnSuccess;
}

UInt64 GA104Framebuffer::getPixelFormatsForDisplayMode(
    IODisplayModeID displayMode, IOIndex depth)
{
    int idx = findModeByID(fModes, fModeCount, displayMode);
    if (idx < 0 && displayMode != fCurrentModeID) return kIOPixelEncodingNotSupported;
    return ((UInt64)kIOBitsPerColorComponent8 << 32) | kIOPixelEncodingRGB444;
}

IOReturn GA104Framebuffer::getPixelInformation(
    IODisplayModeID displayMode, IOIndex depth,
    IOPixelAperture aperture, IOPixelInformation *pixelInfo)
{
    if (!pixelInfo) return kIOReturnBadArgument;
    if (aperture != kIOFBSystemAperture) return kIOReturnUnsupported;

    int idx = findModeByID(fModes, fModeCount, displayMode);
    uint16_t w = (idx >= 0) ? fModes[idx].width : fWidth;
    uint16_t h = (idx >= 0) ? fModes[idx].height : fHeight;

    pixelInfo->bytesPerRow = w * 4;
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
    pixelInfo->activeWidth = w;
    pixelInfo->activeHeight = h;
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
    int idx = findModeByID(fModes, fModeCount, displayMode);
    if (idx < 0 && displayMode != fCurrentModeID) return kIOReturnUnsupported;

    uint16_t w = (idx >= 0) ? fModes[idx].width : fWidth;
    uint16_t h = (idx >= 0) ? fModes[idx].height : fHeight;
    uint8_t  refresh = (idx >= 0) ? fModes[idx].refreshHz : 60;

    IOLog("GA104FB: setDisplayMode %ux%u@%u depth=%lu\n", w, h, refresh, (unsigned long)depth);

    if (fDevice) {
        IOReturn gspRet;
        if (idx >= 0 && fModes[idx].pixelClock > 0) {
            // Use real EDID timings
            GSPModesetParams p;
            bzero(&p, sizeof(p));
            p.headIndex = 0; p.sorIndex = 0;
            p.width = w; p.height = h; p.refreshHz = refresh;
            p.bpp = 32; p.pitch = w * 4;
            p.framebufferAddr = fDevice->getFramebufferAddr();
            p.hTotal = fModes[idx].hTotal;
            p.vTotal = fModes[idx].vTotal;
            p.hSyncStart = fModes[idx].hSyncStart;
            p.hSyncEnd = fModes[idx].hSyncEnd;
            p.vSyncStart = fModes[idx].vSyncStart;
            p.vSyncEnd = fModes[idx].vSyncEnd;
            p.hBlankStart = fModes[idx].hBlankStart;
            p.hBlankEnd = fModes[idx].hBlankEnd;
            p.vBlankStart = fModes[idx].vBlankStart;
            p.vBlankEnd = fModes[idx].vBlankEnd;
            p.clockKHz = fModes[idx].pixelClock / 1000;
            p.colorFormat = NV_PWINDOW_FORMAT_B8G8R8A8;
            gspRet = fDevice->sendGspRpcHeadSetTimings(0, &p);
        } else {
            // Legacy fallback: use auto-computed timings
            gspRet = fDevice->sendGspRpcHeadSetTimings(0, w, h, refresh);
        }
        if (gspRet == kIOReturnSuccess) {
            fDevice->sendGspRpcFlip(0);
        } else {
            // Fallback: direct register programming
            IOLog("GA104FB: GSP modeset failed (0x%x), using legacy path\n", gspRet);
            IOReturn ret = fDevice->programHeadForMode(0, w, h, refresh);
            if (ret != kIOReturnSuccess) {
                IOLog("GA104FB: legacy modeset failed: 0x%x\n", ret);
                return ret;
            }
        }

        fWidth = w; fHeight = h;
        fRefreshRate = (uint32_t)refresh << 16;
    }

    fCurrentDepth = depth;
    fCurrentModeID = displayMode;
    IOLog("GA104FB: setDisplayMode OK (%ux%u)\n", w, h);
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
    *value = 0;

    switch (attribute) {
        case kConnectionFlags:
            *value = 0;
            return kIOReturnSuccess;
        case kConnectionDisplayParameterCount:
            *value = 0;
            return kIOReturnSuccess;
        case kConnectionDisplayParameters:
            *value = 0;
            return kIOReturnSuccess;
        case kConnectionCheckEnable:
            *value = 1;
            return kIOReturnSuccess;
        case kConnectionEnable:
            *value = 1;
            return kIOReturnSuccess;
        case kConnectionSupportsHLDDCSense:
            *value = 0;
            return kIOReturnSuccess;
        case kConnectionSupportsAppleSense:
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
