#include "GA104Device.hpp"
#include "GA104Regs.h"
#include "GA104DeviceUtilities.h"
#include "VBIOSDisplay.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <sys/types.h>
#include <IOKit/IOCommandGate.h>

IOReturn GA104Device::fillFramebuffer(uint32_t color)
{
    if (!fVRAMBase || fFB.fbSize == 0) {
        IOLog("GA104: fillFramebuffer: no FB\n");
        return kIOReturnNotReady;
    }

    // Try to program VPLL for display clock (may not work on Ampere DCE)
    programVPLL();

    // Wait for VPLL lock
    IOSleep(50);

    // Fill entire framebuffer with color (1920x1080×4 = 8,294,400 bytes)
    // XRGB format: lower 8 bits = R, then G, B, A? Depends on byte order
    // On x86 macOS with native color: ARGB = 0xAARRGGBB
    // Red = 0xFFFF0000 (ARGB) or 0x000000FF (BGRA)
    uint32_t redColor = color;
    if (color == 0) redColor = 0x00FF0000;  // default red ARGB

    uint32_t *fb = (uint32_t*)fFB.vramPtr;
    if (!fb) return kIOReturnNotReady;

    uint32_t numPixels = (uint32_t)(fFB.fbSize / 4);
    IOLog("GA104: Filling FB with color 0x%08x (%u pixels)...\n",
          redColor, numPixels);

    // Fill in chunks for speed (128 bytes = 32 pixels at a time)
    for (uint32_t i = 0; i < numPixels; i++) {
        fb[i] = redColor;
    }
    __sync_synchronize();

    IOLog("GA104: FB filled — screen should be RED\n");
    setProperty("GA104GSP_FillColor", redColor, 32);
    setProperty("GA104GSP_FillPixels", numPixels, 32);
    return kIOReturnSuccess;
}
IOReturn GA104Device::programVPLL()
{
    if (!fBar0Virt) return kIOReturnNotReady;

    // For 1920x1080@60, pixel clock = 148.5 MHz
    // Reference clock = 27 MHz (typical display PLL ref)
    // Formula: PCLK = REF * (N / M) / P
    // Using: N=44, M=2, P=4 → 27 * 22 / 4 = 148.5 MHz
    uint32_t n = 44;
    uint32_t m = 2;
    uint32_t p = 4;

    // NV_PVPLL_CONFIG: set up for high freq
    writeAbsReg32(NV_PVPLL_CONFIG(0), NV_PVPLL_CONFIG_DEFAULT);
    IODelay(10);

    // NV_PVPLL_N_FN: fractionalN = 0, N = n, M = m
    uint32_t n_fn = (n << 16) | m;  // N in upper 16 bits, M in lower
    writeAbsReg32(NV_PVPLL_N_FN(0), n_fn);
    IODelay(10);

    // NV_PVPLL_P_M: P divider (bit 0-7: post-divider)
    writeAbsReg32(NV_PVPLL_P_M(0), p);
    IODelay(10);

    // Enable VPLL
    writeAbsReg32(NV_PVPLL_ENABLE(0), NV_PVPLL_ENABLE_ON);
    IODelay(100);

    IOLog("GA104: VPLL programmed (N=%u, M=%u, P=%u)\n", n, m, p);
    setProperty("GA104VPLL_Programmed", true, 8);
    return kIOReturnSuccess;
}
IOReturn GA104Device::flipToTriangle()
{
    if (!fVRAMBase || !fBar1Phys) return kIOReturnNotReady;

    uint64_t triOff = 0x300000;
    uint32_t w = 1920, h = 1080;
    uint32_t bufSize = w * h * 4;

    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(
        fBar1Phys + triOff, bufSize, kIODirectionOut);
    if (!md) return kIOReturnNoMemory;
    md->prepare();

    // Rasterizar triângulo com gradiente RGB barycentric
    // Vertices: A(960,100) red, B(100,900) green, C(1820,900) blue
    float ax = 960, ay = 100;
    float bx = 100, by = 900;
    float cx = 1820, cy = 900;

    float denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float px = (float)x, py = (float)y;

            // Barycentric coordinates
            float wA = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denom;
            float wB = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denom;
            float wC = 1.0f - wA - wB;

            uint32_t color = 0xFF000000; // preto alpha full

            if (wA >= 0 && wB >= 0 && wC >= 0) {
                // Vertex colors: ABGR (macOS native pixel format)
                uint8_t r = (uint8_t)(wA * 255.0f); // red from vertex A
                uint8_t g = (uint8_t)(wB * 255.0f); // green from vertex B
                uint8_t b = (uint8_t)(wC * 255.0f); // blue from vertex C
                color = 0xFF000000 | (b << 16) | (g << 8) | r; // XRGB (little-endian)
            }

            uint32_t offset = (y * w + x) * 4;
            md->writeBytes(offset, &color, 4);
        }
    }
    md->complete();
    md->release();

    // Flip HEAD_BASE para o buffer do triângulo
    writeAbsReg32(NV_PHEAD_SET_BASE(0), (uint32_t)(triOff & 0xFFFFFFFF));
    writeAbsReg32(NV_PHEAD_SET_BASE_LIGHT(0), (uint32_t)(triOff & 0xFFFFFFFF));
    __sync_synchronize();

    IOLog("GA104: Flipped to triangle buffer @ 0x%llx\n", triOff);
    setProperty("GA104GSP_TriangleFlip", true, 8);
    return kIOReturnSuccess;
}
IOReturn GA104Device::flipToFramebuffer()
{
    if (!fBar0Virt) return kIOReturnNotReady;
    writeAbsReg32(NV_PHEAD_SET_BASE(0), 0);
    writeAbsReg32(NV_PHEAD_SET_BASE_LIGHT(0), 0);
    __sync_synchronize();
    IOLog("GA104: Flipped back to framebuffer @ 0\n");
    setProperty("GA104GSP_FBFlip", true, 8);
    return kIOReturnSuccess;
}
IOReturn GA104Device::setupFramebuffer()
{
    // Configure framebuffer: VRAM offset 0x20000000, 1920x1080x32bpp
    fFB.width  = 1920;
    fFB.height = 1080;
    fFB.pitch  = fFB.width * 4;   // 32bpp = 7680 bytes per line
    fFB.bpp    = 32;
    fFB.fbAddr = 0;
    fFB.fbSize = fFB.pitch * fFB.height;

    setProperty("GA104FBAddr", fFB.fbAddr, 64);
    setProperty("GA104FBSize", fFB.fbSize, 64);
    setProperty("GA104FBRes", (fFB.width << 16) | fFB.height, 32);
    IOLog("GA104: FB: 1920x1080@32bpp addr=0x%llx size=%llu\n", fFB.fbAddr, fFB.fbSize);

    // Update display engine with FB address
    writeReg32(NV_PWINDOW_SET_BASE(0), (uint32_t)(fFB.fbAddr & 0xFFFFFFFF));
    writeReg32(NV_PWINDOW_SET_SIZE(0), (fFB.height << 16) | fFB.width);
    writeReg32(NV_PWINDOW_SET_PITCH(0), fFB.pitch);
    writeReg32(NV_PWINDOW_SET_FORMAT(0), NV_PWINDOW_FORMAT_B8G8R8A8);
    __sync_synchronize();

    // Try to map VRAM framebuffer into kernel space for direct pixel writes
    // Option A: Use BAR1 mapping (fBAR1Virt already maps BAR1, but limited to 256MB)
    // Option B: Create a new mapping for the FB area
    fFB.vramPtr = fVRAMBase + fFB.fbAddr;  // Direct VRAM access via mapped BAR1

    if (fFB.vramPtr) {
        IOLog("GA104: FB mapped at kernel VA %p\n", fFB.vramPtr);
        setProperty("GA104FBKernelVA", (uint64_t)(uintptr_t)fFB.vramPtr, 64);

        IOLog("GA104: FB setup complete\n");
        setProperty("GA104FBDone", true, 8);
    } else {
        IOLog("GA104: FB mapping FAILED\n");
        setProperty("GA104FBFail", true, 8);
        return kIOReturnNoMemory;
    }

    return kIOReturnSuccess;
}
IOReturn GA104Device::setupDisplayChannels()
{
    IOLog("GA104: setupDisplayChannels: core + window pushbuffers\n");

    // Pushbuffer addresses in VRAM (right after our FB at 0x1000000 + FB size)
    uint64_t corePbAddr = fFB.fbAddr + fFB.fbSize;     // ~0x1070000
    uint64_t wndwPbAddr = corePbAddr + DISP_PB_CORE_SIZE; // ~0x1080000

    // Align to 256 bytes
    corePbAddr = (corePbAddr + 0xFF) & ~0xFF;
    wndwPbAddr = (wndwPbAddr + 0xFF) & ~0xFF;

    // Allocate pushbuffer memory in VRAM (zero it)
    if (fVRAMBase) {
        memset(fVRAMBase + corePbAddr, 0, DISP_PB_CORE_SIZE + DISP_PB_WINDOW_SIZE);
        __sync_synchronize();
    }

    // Core channel pushbuffer address (target=1=VRAM, addr >> 8)
    uint64_t corePbVal = 1 | (corePbAddr >> 8);   // target | address>>8
    writeReg32(NV_PDISP_CORE_PB_ADDR_LO, (uint32_t)(corePbVal & 0xFFFFFFFF));
    writeReg32(NV_PDISP_CORE_PB_ADDR_HI, (uint32_t)(corePbVal >> 32));
    writeReg32(NV_PDISP_CORE_PB_CTL1, 0x00000001);
    writeReg32(NV_PDISP_CORE_PB_CTL2, 0x00000040);
    __sync_synchronize();

    // Reset core channel: disable, set PUT=GET=0, re-enable
    writeReg32(NV_PDISP_CORE_CHANNEL_CTL, 0x00000000);
    __sync_synchronize();
    IODelay(100);
    writeReg32(NV_PDISP_CORE_PUT, 0);  // Reset PUT and GET to 0
    writeReg32(NV_PDISP_CORE_GET, 0);
    __sync_synchronize();

    // Re-enable core channel with new pushbuffer
    writeReg32(NV_PDISP_CORE_CHANNEL_CTL, 0x00000002);  // just enable
    __sync_synchronize();
    IODelay(100);
    writeReg32(NV_PDISP_CORE_CHANNEL_CTL, 0x00000013);  // enable + activate
    __sync_synchronize();
    IODelay(100);

    // Window channel 0 pushbuffer address
    uint64_t wndwPbVal = 1 | (wndwPbAddr >> 8);
    writeReg32(NV_PDISP_WINDOW_PB_ADDR_LO, (uint32_t)(wndwPbVal & 0xFFFFFFFF));
    writeReg32(NV_PDISP_WINDOW_PB_ADDR_HI, (uint32_t)(wndwPbVal >> 32));
    writeReg32(NV_PDISP_WINDOW_PB_CTL1, 0x00000001);
    writeReg32(NV_PDISP_WINDOW_PB_CTL2, 0x00000040);
    __sync_synchronize();

    // Enable + activate window channel 0 (reset + re-init)
    writeReg32(NV_PDISP_WINDOW_CHANNEL_CTL, 0x00000000);
    __sync_synchronize();
    IODelay(100);
    writeReg32(NV_PDISP_WINDOW_PUT, 0);
    writeReg32(NV_PDISP_WINDOW_GET, 0);
    __sync_synchronize();
    writeReg32(NV_PDISP_WINDOW_CHANNEL_CTL, 0x00000002);
    __sync_synchronize();
    IODelay(100);
    writeReg32(NV_PDISP_WINDOW_CHANNEL_CTL, 0x00000013);
    __sync_synchronize();

    fFB.corePbAddr = corePbAddr;
    fFB.wndwPbAddr = wndwPbAddr;
    setProperty("GA104PBCore", corePbAddr, 64);
    setProperty("GA104PBWindow", wndwPbAddr, 64);
    IOLog("GA104: channels ready: core PB=0x%llx wndw PB=0x%llx\n", corePbAddr, wndwPbAddr);
    return kIOReturnSuccess;
}
IOReturn GA104Device::legacyDisplayInit()
{
    IOLog("GA104: legacyDisplayInit starting (no GSP)\n");

    // STEP 1: Claim display ownership
    uint32_t own = readReg32(NV_PDISP_OWNERSHIP);
    if (own & 0x00000002) {
        IOLog("GA104: disp owned by another engine, releasing\n");
        writeReg32(NV_PDISP_OWNERSHIP, own & ~0x00000001);
        for (int i = 0; i < 200; i++) {
            if (!(readReg32(NV_PDISP_OWNERSHIP) & 0x00000002)) break;
            IODelay(10);
        }
        IOLog("GA104: disp ownership released\n");
    }

    // STEP 2: Lock pin capabilities
    writeReg32(NV_PDISP_PIN_CAPS, 0x00000021);

    // STEP 3: Detect heads and SORs
    uint32_t hdrMask = readReg32(NV_PDISP_HEAD_MASK);
    uint32_t sorCount = (hdrMask >> 8) & 0xFF;
    uint32_t headCount = hdrMask & 0xFF;
    if (sorCount == 0) sorCount = 4;  // GA104 typical
    if (headCount == 0) headCount = 4;
    IOLog("GA104: disp heads=%u SORs=%u\n", headCount, sorCount);
    setProperty("GA104DispHeads", headCount, 32);
    setProperty("GA104DispSORs", sorCount, 32);

    // STEP 4: Program shadow registers (SOR capabilities)
    for (uint32_t i = 0; i < sorCount; i++) {
        uint32_t caps = readReg32(NV_PDISP_SOR_CAP_HW(i));
        writeReg32(NV_PDISP_SOR_CAP_SHADOW_CTL,
                   readReg32(NV_PDISP_SOR_CAP_SHADOW_CTL) | (0x100 << i));
        writeReg32(NV_PDISP_SOR_CAP_SHADOW(i), caps);
    }

    // STEP 5: Program head capabilities (RG + POSTCOMP)
    for (uint32_t i = 0; i < headCount; i++) {
        uint32_t rg = readReg32(NV_PDISP_HEAD_RG_HW(i));
        writeReg32(NV_PDISP_HEAD_RG_SHADOW(i), rg);
        for (int j = 0; j < 20; j += 4) {
            uint32_t pc = readReg32(NV_PDISP_HEAD_POSTCOMP_HW(i) + j);
            writeReg32(NV_PDISP_HEAD_POSTCOMP_SHADOW(i) + j, pc);
        }
    }

    // STEP 6: Program window capabilities
    uint32_t wndwCount = 8;  // GA104 typical
    for (uint32_t i = 0; i < wndwCount; i++) {
        writeReg32(NV_PDISP_WNDW_CAP_SHADOW_CTL,
                   readReg32(NV_PDISP_WNDW_CAP_SHADOW_CTL) | (1 << i));
        for (int j = 0; j < 24; j += 4) {
            uint32_t wc = readReg32(NV_PDISP_WNDW_CAP_HW(i) + j);
            writeReg32(NV_PDISP_WNDW_CAP_SHADOW(i) + j, wc);
        }
    }
    writeReg32(NV_PDISP_IHUB_CAP_SHADOW_CTL,
               readReg32(NV_PDISP_IHUB_CAP_SHADOW_CTL) | 0x100);

    // STEP 7: Program IHUB capabilities
    for (int i = 0; i < 3; i++) {
        uint32_t ih = readReg32(NV_PDISP_IHUB_CAP_HW(i));
        writeReg32(NV_PDISP_IHUB_CAP_SHADOW(i), ih);
    }

    // STEP 8: Enable display engine
    writeReg32(NV_PDISP_ENABLE, readReg32(NV_PDISP_ENABLE) | 0x00000001);
    __sync_synchronize();

    // STEP 9: Setup instance memory — allocate separate buffer in VRAM
    // Old: used elfAddr (firmware base) — WRONG, overwrites firmware.
    // New: allocate VRAM at a known offset for instance memory
    uint64_t instAddr = 0x00F00000;  // 15MB into VRAM, safe space
    uint32_t instSize = 0x10000;      // 64KB instance memory
    
    // Zero the instance memory in VRAM
    if (fVRAMBase) {
        memset(fVRAMBase + instAddr, 0, instSize);
        __sync_synchronize();
        
        // Write correct context DMA objects in instance memory
        // Each context DMA entry = 5 dwords at 32-byte aligned boundary
        // OBJ 0 at offset 0x00 (inst[0..7]): NULL entry (8 dwords = 32 bytes, all zero)
        // OBJ 1 at offset 0x20 (inst[8..12]): DMA descriptor (5 dwords)
        volatile uint32_t *inst = (volatile uint32_t*)(fVRAMBase + instAddr);
        // Clear entire instance memory header (first 64 bytes)
        for (int i = 0; i < 16; i++) inst[i] = 0;
        
        // OBJ 1 at inst[8..12]: Context DMA for VRAM framebuffer access
        // Format from dev_disp.h v03_00:
        //   word 0: target(1=VRAM) | access(RW=1<<2) | kind(PITCH=0) = 0x05
        //   word 1: BASE_LO  = (address >> 8) & 0xFFFFFFFF
        //   word 2: BASE_HI  = (address >> 40) & 0x7F
        //   word 3: LIMIT_LO = ((address + size - 1) >> 8) & 0xFFFFFFFF  
        //   word 4: LIMIT_HI = ((address + size - 1) >> 40) & 0x7F
        uint64_t fbAddr64 = fFB.fbAddr;
        uint64_t fbSize64 = fFB.fbSize;
        uint64_t fbEnd    = fbAddr64 + fbSize64 - 1;
        
        inst[8]  = 0x00000005;                       // target=VRAM(1) | access=RW(2) | kind=PITCH(0)
        inst[9]  = (uint32_t)(fbAddr64 >> 8);        // BASE_LO
        inst[10] = (uint32_t)(fbAddr64 >> 40) & 0x7F; // BASE_HI
        inst[11] = (uint32_t)(fbEnd >> 8);           // LIMIT_LO
        inst[12] = (uint32_t)(fbEnd >> 40) & 0x7F;   // LIMIT_HI
        __sync_synchronize();
    }
    
    writeReg32(NV_PDISP_INST_MEM_TARGET, 0x00000008 | 0x00000001);
    writeReg32(NV_PDISP_INST_MEM_ADDR, instAddr >> 16);
    __sync_synchronize();

    // STEP 10: Enable interrupts
    writeReg32(NV_PDISP_INTR_CTRL_MSK, 0x00000187);
    writeReg32(NV_PDISP_INTR_CTRL_EN, 0x00000187);
    writeReg32(NV_PDISP_INTR_OR_MSK, 0x000000FF);
    writeReg32(NV_PDISP_INTR_OR_EN, 0x000000FF);
    for (uint32_t i = 0; i < headCount; i++) {
        writeReg32(NV_PDISP_INTR_HEAD_MSK(i), 0x00000004);
        writeReg32(NV_PDISP_INTR_HEAD_EN(i), 0x00000004);
    }

    // STEP 11: VPLL on Ampere is managed by DCE (Display Control Engine), not legacy VPLL regs.
    // Without GSP firmware, we can't program the DCE PLL. EFI GOP clock is used instead.
    // STEP 11: VPLL is DCE-managed on Ampere - can't program without GSP
    IOLog("GA104: VPLL skipped\n");

    // STEP 12: Program head 0 timing (1920x1080@60)
    writeReg32(NV_PHEAD_SET_HEAD_TIMING(0),
               (TIMING_1920x1080_60_VTOTAL << 16) | TIMING_1920x1080_60_HTOTAL);
    writeReg32(NV_PHEAD_SET_HEAD_VSYNC(0),
               (TIMING_1920x1080_60_VSYNC_END << 16) | TIMING_1920x1080_60_HSYNC_END);
    writeReg32(NV_PHEAD_SET_HEAD_BLANK(0),
               (TIMING_1920x1080_60_VBLANK_START << 16) | TIMING_1920x1080_60_HBLANK_START);
    writeReg32(NV_PHEAD_SET_HEAD_BLACK(0),
               (TIMING_1920x1080_60_VBLANK_END << 16) | TIMING_1920x1080_60_HBLANK_END);
    writeReg32(NV_PHEAD_SET_PIXEL_CLOCK(0), TIMING_1920x1080_60_PCLOCK_KHZ);
    writeReg32(NV_PHEAD_SET_CONTROL(0), NV_PHEAD_SET_CONTROL_DEPTH_24BPP | 0x1);  // enable head
    __sync_synchronize();

    // STEP 13: Power on SOR 0 for HDMI (Ampere clock register)
    writeReg32(NV_PSOR_CLK_AMPERE(0), NV_PSOR_CLK_AMPERE_TMDS);
    writeReg32(NV_PSOR_POWER_STATE(0), NV_PSOR_POWER_ON);
    for (int i = 0; i < 100; i++) {
        if (!(readReg32(NV_PSOR_POWER_STATE(0)) & NV_PSOR_POWER_BUSY)) break;
        IODelay(10);
    }
    IOLog("GA104: SOR0 powered on\n");

    // STEP 14: Set framebuffer window 0
    // FB base at VRAM offset (somewhere unused, or BAR2 mapping)
    uint64_t fbAddr = 0;  // VRAM offset 0
    writeReg32(NV_PWINDOW_SET_BASE(0), (uint32_t)(fbAddr & 0xFFFFFFFF));
    writeReg32(NV_PWINDOW_SET_SIZE(0), (1080 << 16) | 1920);
    writeReg32(NV_PWINDOW_SET_PITCH(0), 1920 * 4);  // 32bpp
    writeReg32(NV_PWINDOW_SET_FORMAT(0), NV_PWINDOW_FORMAT_B8G8R8A8);
    __sync_synchronize();

    // STEP 15: Set scanout base to VRAM offset (HEAD_BASE = fbAddr)
    writeReg32(NV_PHEAD_SET_BASE(0), (uint32_t)(fbAddr & 0xFFFFFFFF));
    writeReg32(NV_PHEAD_SET_BASE_LIGHT(0), (uint32_t)(fbAddr & 0xFFFFFFFF));

    // Diagnostic: read hline/vline to confirm scanout is live
    uint32_t hline = readAbsReg32(NV_PHEAD_HLINE(0));
    uint32_t vline = readAbsReg32(NV_PHEAD_VLINE(0));
    IOLog("GA104: legacyDisplayInit complete (FB=0x%llx hline=%u vline=%u)\n", fbAddr, hline, vline);

    setProperty("GA104DispInit", true, 8);
    setProperty("GA104DispFBAddr", fbAddr, 64);
    setProperty("GA104HLine", hline, 32);
    setProperty("GA104VLine", vline, 32);
    return kIOReturnSuccess;
}
IOReturn GA104Device::programHeadForMode(uint32_t head, uint32_t width, uint32_t height, uint32_t refreshHz)
{
    if (head > 3) return kIOReturnBadArgument;
    IOLog("GA104: programHeadForMode head=%u %ux%u@%uHz\n", head, width, height, refreshHz);

    if (!fBar0Virt) {
        IOLog("GA104: programHeadForMode: BAR0 not mapped\n");
        return kIOReturnNotReady;
    }

    // CVT-RB v1 timings for arbitrary resolution
    // VESA CVT Reduced Blanking: fixed 48-pixel horizontal blanking
    uint32_t hVisible = width, vVisible = height;
    uint32_t hSyncWidth = 32, hFrontPorch = 8, hBackPorch = 8;
    uint32_t hSyncStart = hVisible + hFrontPorch;
    uint32_t hSyncEnd = hSyncStart + hSyncWidth;
    uint32_t hTotal = hSyncEnd + hBackPorch;
    uint32_t vSyncWidth = 6, vFrontPorch = 3, vBackPorch = 5;
    uint32_t vSyncStart = vVisible + vFrontPorch;
    uint32_t vSyncEnd = vSyncStart + vSyncWidth;
    uint32_t vTotal = vSyncEnd + vBackPorch;
    uint32_t pixelClockKHz = (uint32_t)((uint64_t)hTotal * vTotal * 60 / 1000);

    uint32_t hTiming = (vTotal << 16) | hTotal;
    uint32_t vSync = (vSyncEnd << 16) | hSyncEnd;
    uint32_t hBlank = (hTotal << 16) | hVisible;
    uint32_t vBlank = (vTotal << 16) | vVisible;

    writeReg32(NV_PHEAD_SET_HEAD_TIMING(head), hTiming);
    writeReg32(NV_PHEAD_SET_HEAD_VSYNC(head), vSync);
    writeReg32(NV_PHEAD_SET_HEAD_BLANK(head), hBlank);
    writeReg32(NV_PHEAD_SET_HEAD_BLACK(head), vBlank);
    writeReg32(NV_PHEAD_SET_PIXEL_CLOCK(head), pixelClockKHz);

    // Set head control: enable + 24bpp
    uint32_t headCtrl = NV_PHEAD_SET_CONTROL_DEPTH_24BPP | 0x1;
    writeReg32(NV_PHEAD_SET_CONTROL(head), headCtrl);
    __sync_synchronize();

    // Program window 0 for this head
    writeReg32(NV_PWINDOW_SET_SIZE(head), (height << 16) | width);
    writeReg32(NV_PWINDOW_SET_PITCH(head), width * 4);
    writeReg32(NV_PWINDOW_SET_FORMAT(head), NV_PWINDOW_FORMAT_B8G8R8A8);

    // Set FB address (VRAM offset)
    uint32_t fbAddr = (uint32_t)(fFB.fbAddr & 0xFFFFFFFF);
    writeReg32(NV_PWINDOW_SET_BASE(head), fbAddr);
    writeReg32(NV_PHEAD_SET_BASE(head), fbAddr);
    writeReg32(NV_PHEAD_SET_BASE_LIGHT(head), fbAddr);
    __sync_synchronize();

    // Power on SOR (use SOR 0 for head 0, SOR 1 for head 1, etc.)
    uint32_t sorIndex = head;
    writeReg32(NV_PSOR_CLK_AMPERE(sorIndex), NV_PSOR_CLK_AMPERE_TMDS);
    writeReg32(NV_PSOR_POWER_STATE(sorIndex), NV_PSOR_POWER_ON);
    for (int i = 0; i < 100; i++) {
        if (!(readReg32(NV_PSOR_POWER_STATE(sorIndex)) & NV_PSOR_POWER_BUSY)) break;
        IODelay(10);
    }

    // Update stored resolution
    fFB.width = width;
    fFB.height = height;
    fFB.pitch = width * 4;

    // Check scanout
    uint32_t hline = readAbsReg32(NV_PHEAD_HLINE(head));
    uint32_t vline = readAbsReg32(NV_PHEAD_VLINE(head));
    IOLog("GA104: programHeadForMode done (hline=%u vline=%u)\n", hline, vline);

    setProperty("GA104ModeWidth", width, 32);
    setProperty("GA104ModeHeight", height, 32);
    setProperty("GA104HLine", hline, 32);
    setProperty("GA104VLine", vline, 32);

    return kIOReturnSuccess;
}

