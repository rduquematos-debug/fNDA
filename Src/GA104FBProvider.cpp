#include "GA104FBProvider.hpp"
#include "GA104Device.hpp"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(GA104FBProvider, IOService)

static GA104Device *gDevRef = nullptr;

static bool fbAppeared(void *target, void *refCon, IOService *newService, IONotifier *notifier)
{
    if (newService) {
        newService->setProperty("IOFBProbeScore", 25001ULL, 32);
        newService->setProperty("display-type", "NVIDIA");
        IOLog("GA104FB: IONDRVFramebuffer properties set\n");
    }
    return true;
}

bool GA104FBProvider::init(GA104Device *device)
{
    if (!super::init()) return false;
    fDevice = device;
    return true;
}

void GA104FBProvider::free()
{
    fDevice = nullptr;
    super::free();
}

bool GA104FBProvider::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    gDevRef = fDevice;

    setProperty("IOFBProbeScore", 25001ULL, 32);
    setProperty("display-type", "NVIDIA");
    setProperty("device_type", "display");
    setProperty("model", "NVIDIA GeForce RTX 3070 Ti");

    uint8_t rendererId[] = {0x08, 0x00, 0x04, 0x01};
    setProperty("IOVARendererID", rendererId, sizeof(rendererId));
    uint8_t rendererSubId[] = {0x03, 0x00, 0x00, 0x00};
    setProperty("IOVARendererSubID", rendererSubId, sizeof(rendererSubId));

    if (fDevice) {
        uint64_t vramMB = fDevice->getBAR1Size() / (1024 * 1024);
        setProperty("VRAM,totalMB", vramMB, 32);
        setProperty("GA104VRAMSize", fDevice->getBAR1Size(), 64);
        setProperty("GA104BAR1Phys", fDevice->getBAR1Phys(), 64);
    }

    registerService();

    // Register notification for IONDRVFramebuffer (property injection only, no vtable/fVramMap)
    OSDictionary *matching = IOService::serviceMatching("IONDRVFramebuffer");
    if (matching) {
        IOService::addMatchingNotification(gIOFirstMatchNotification, matching, fbAppeared, nullptr, nullptr);
        OSIterator *iter = IOService::getMatchingServices(matching);
        if (iter) {
            OSObject *obj;
            while ((obj = iter->getNextObject()))
                fbAppeared(nullptr, nullptr, OSDynamicCast(IOService, obj), nullptr);
            iter->release();
        }
        matching->release();
    }

    IOLog("GA104FBProvider: published as IOService\n");
    return true;
}

void GA104FBProvider::stop(IOService *provider)
{
    gDevRef = nullptr;
    super::stop(provider);
}

IOReturn GA104FBProvider::setGPUPolicy(OSDictionary *policy)
{
    return kIOReturnSuccess;
}
