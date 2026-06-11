#include "GA104FBProvider.hpp"
#include "GA104Device.hpp"
#include "GA104Framebuffer.hpp"
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(GA104FBProvider, IOService)

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

    // Create GA104Framebuffer as child nub
    GA104Framebuffer *fb = new GA104Framebuffer;
    if (fb) {
        uint32_t width = 1920, height = 1080;
        uint64_t fbOffset = fDevice ? fDevice->getFramebufferAddr() : 0;
        uint64_t fbSize = fDevice ? fDevice->getFramebufferSize() : (width * height * 4);
        uint64_t fbPhys = (fDevice ? fDevice->getBAR1Phys() : 0) + fbOffset;
        if (fb->init(fDevice, fbPhys, fbSize, width, height)) {
            if (fb->attach(this)) {
                fb->registerService();
                IOLog("GA104FB: Framebuffer attached and registered\n");
            } else {
                IOLog("GA104FB: Framebuffer attach failed\n");
                fb->release();
            }
        } else {
            IOLog("GA104FB: Framebuffer init failed\n");
            fb->release();
        }
    }

    IOLog("GA104FBProvider: started as IOService child of GA104Device\n");
    return true;
}

void GA104FBProvider::stop(IOService *provider)
{
    super::stop(provider);
}

IOReturn GA104FBProvider::setGPUPolicy(OSDictionary *policy)
{
    return kIOReturnSuccess;
}
