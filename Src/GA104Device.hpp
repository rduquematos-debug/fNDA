#ifndef GA104Device_hpp
#define GA104Device_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "GA104Regs.h"
#include "GSPFirmware.hpp"
#include "GSPQueue.hpp"
#include "GSPProtocol.hpp"
#include "VBIOSDisplay.hpp"

class GA104FBProvider;

#define GA104_GSP_FW_SIZE       0x5000000

class GA104Device : public IOService
{
    OSDeclareDefaultStructors(GA104Device);
    friend class GA104UserClient;

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual bool init(OSDictionary *dict = nullptr) override;
    virtual void free() override;
    virtual IOReturn newUserClient(task_t owningTask, void *securityID,
                                    UInt32 type, OSDictionary *properties,
                                    IOUserClient **handler) override;

    IOService *getProvider() const { return fProvider; }

    uint32_t getDeviceID() const { return fDeviceID; }
    uint32_t getRevision() const { return fRevision; }
    uint64_t getBAR0Phys() const { return fBar0Phys; }
    uint64_t getBAR0Size() const { return fBAR0Map ? fBAR0Map->getLength() : 0; }
    uint64_t getBAR1Phys() const { return fBar1Phys; }
    uint64_t getBAR1Size() const { return fBar1Size; }
    uint64_t getBAR2Phys() const { return fBar2Phys; }
    uint64_t getBAR2Size() const { return fVRAMSize; }
    GSPFirmware *getGSPFirmware() { return fGSPFirmware; }

    IOReturn loadGSPFirmware(void);
    IOReturn bootGSP(void);
    IOReturn bootSEC2(void);
    IOReturn setCOTPayload(const uint8_t *data, uint32_t size);
    IOReturn populateWprMeta(void);
    IOReturn setupRadix3(void);
    IOReturn createFWBuffer(uint32_t size);
    IOReturn createBLBuffer(uint32_t size);
    void     *getFWBufferAddr() const { return fFWBuffer; }
    uint64_t getFWBufferPhys() const { return fFWBufferPhys; }
    uint32_t getFWBufferSize() const { return fFWBufferSize; }

    void     *getBootloaderAddr() const { return fBootloaderBuffer; }
    uint64_t getBootloaderPhys() const { return fBootloaderPhys; }
    uint32_t getBootloaderSize() const { return fBootloaderSize; }

    uint32_t readReg32(uint32_t offset);
    void writeReg32(uint32_t offset, uint32_t value);
    uint32_t writeAbsReg32(uint32_t absOffset, uint32_t value);
    uint32_t readAbsReg32(uint32_t absOffset);

    // Post-boot GSP RPC
    IOReturn sendGspRpc(GspRpcMessageHeader *msg, void *payload,
                         uint32_t payloadSize,
                         GspRpcMessageHeader *reply, uint32_t replyMaxSize,
                         uint32_t *replySize, uint32_t timeoutMs);
    IOReturn sendGspRpcAllocRoot(void);
    IOReturn sendGspRpcAllocDisplayChain(void);
    IOReturn fillFramebuffer(uint32_t color);
    IOReturn programVPLL(void);
    IOReturn flipToTriangle(void);
    IOReturn flipToFramebuffer(void);
    IOReturn readCSRs(void);

private:
    IOService         *fProvider;
    GA104FBProvider   *fFBProvider;
    GSPFirmware       *fGSPFirmware;
    GSPQueue      *fGSPQueue;
    GSPProtocol   *fGSPProtocol;
    VBIOSDisplay  *fVBIOSDisplay;

    IOMemoryMap   *fBAR0Map;
    IOMemoryMap   *fBAR1Map;
    IOMemoryMap   *fBAR2Map;

    // Firmware buffer (system memory)
    void          *fFWBuffer;
    uint64_t       fFWBufferPhys;
    uint32_t       fFWBufferSize;

    // Bootloader buffer (separate from main firmware)
    void          *fBootloaderBuffer;
    uint64_t       fBootloaderPhys;
    uint32_t       fBootloaderSize;

    // Phase 2: VRAM layout
    struct {
        uint64_t wpr2Addr;
        uint64_t wpr2Size;
        uint64_t heapAddr;
        uint64_t heapSize;
        uint64_t elfAddr;
        uint64_t elfSize;
        uint64_t bootAddr;
        uint64_t bootSize;
        uint64_t frtsAddr;
        uint64_t frtsSize;
        uint64_t fbSize;
        uint64_t queueOffset;    // offset within heap for cmdq/msgq
        uint64_t queuePhysAddr;  // BAR1 physical address of queues (firmware-accessible via WPR2)
    } fVramLayout;

    // Phase 2: system memory buffers
    void          *fWprMetaBuf;
    uint64_t       fWprMetaPhys;
    uint32_t       fWprAddr;
    uint32_t       fLastFwVal;
    void          *fSec2SigBuf;
    uint64_t       fSec2SigPhys;
    void          *fRadix3Buf;
    uint64_t       fRadix3Phys;
    void          *fLibosBuf;
    uint64_t       fLibosPhys;
    void          *fShmBuf;
    uint64_t       fShmPhys;
    void          *fLogInitBuf;
    uint64_t       fLogInitPhys;
    void          *fLogIntrBuf;
    uint64_t       fLogIntrPhys;
    void          *fLogRmBuf;
    uint64_t       fLogRmPhys;
    void          *fRmargsBuf;
    uint64_t       fRmargsPhys;
    uint32_t       fFwEntryPoint;
    uint8_t        fCOTPayload[864];
    bool           fHasCOTPayload;

    // Framebuffer (direct VRAM, no GSP)
    struct {
        uint64_t fbAddr;       // VRAM offset
        uint64_t fbSize;       // total bytes
        uint32_t width, height, pitch, bpp;
        void     *vramPtr;     // kernel VA for pixel writes
        uint64_t vblankCount;
        uint64_t corePbAddr;   // core pushbuffer VRAM offset
        uint64_t wndwPbAddr;   // window0 pushbuffer VRAM offset
    } fFB;
    IOMemoryMap  *fFBMap;

    uint8_t       *fBar0Virt;
    uint64_t       fBar0Phys;
    uint8_t       *fBar1Virt;
    uint64_t       fBar1Phys;
    uint64_t       fBar1Size;
    uint64_t       fBar2Phys;
    uint8_t       *fGSPBase;
    uint8_t       *fVRAMBase;

    uint64_t       fBDF;               // PCI Bus:Device.Function encoding

    uint32_t       fDeviceID;
    uint32_t       fRevision;
    uint64_t       fVRAMSize;
    bool           fGSPBooted;
    bool           fSEC2Booted;

    // Booter v3 state (encrypted Direct Booter Boot)
    bool             fBooterV3;
    uint32_t         fBooterImemSz;
    uint32_t         fBooterDmemSz;
    uint32_t         fBooterManifestOff;
    uint32_t         fBooterEngMask;
    uint32_t         fBooterUcode;
    uint32_t         fBooterAppVer;

    // Post-boot RPC queue state
    GspMsgqTxHeader *fCmdqTx;
    GspMsgqTxHeader *fMsgqTx;
    uint8_t         *fCmdqEntryBase;
    uint8_t         *fMsgqEntryBase;
    uint8_t         *fVramCmdqEntryBase;
    uint8_t         *fVramMsgqEntryBase;
    uint64_t         fCmdqOff;
    uint64_t         fMsgqOff;
    uint32_t         fLastMsgqRp;
    uint8_t         *fFwImageData;     // saved fwImg pointer for post-boot VRAM re-copy
    uint32_t         fFwImageSize;

    // RM object handles (from GSP allocs)
    NvHandle         fRmRoot;
    NvHandle         fRmDevice;
    NvHandle         fRmSubdevice;
    NvHandle         fRmDisp;

    // GOP preserved state
    struct {
        uint32_t headBase;
        uint32_t timing;
        uint32_t pixelClock;
    } fGOP;

    // EDID buffer
    uint8_t          fEDID[256];
    uint32_t         fEDIDSize;

    IOReturn mapBars(void);
    IOReturn identifyDevice(void);
    IOReturn calculateVramLayout(void);
    IOReturn buildRadix3PageTable(uint32_t fwSize);
    IOReturn setupWpr2(void);
    IOReturn gspSetupQueues(void);
    IOReturn loadBLtoFalcon(void);
    IOReturn gspStartBooter(void);
    IOReturn patchFirmwareForBooter(void);
    void     applyFirmwarePatches(uint8_t *fw, uint32_t sz, bool fullCounters);
    IOReturn legacyDisplayInit(void);
    IOReturn setupFramebuffer(void);
    IOReturn setupDisplayChannels(void);
public:
    IOReturn programHeadForMode(uint32_t head, uint32_t width, uint32_t height, uint32_t refreshHz);
    IOReturn readEDID(uint8_t *edid, uint32_t maxSize);
    uint8_t *getEDID() { return fEDID; }
    uint32_t getEDIDSize() const { return fEDIDSize; }
    uint64_t getFramebufferAddr() const { return fFB.fbAddr; }
    uint64_t getFramebufferSize() const { return fFB.fbSize; }
    void     *getFramebufferPtr() const { return fFB.vramPtr; }
    uint32_t getFramebufferWidth() const { return fFB.width; }
    uint32_t getFramebufferHeight() const { return fFB.height; }
    uint32_t getFramebufferPitch() const { return fFB.pitch; }
    uint64_t getFramebufferVblankCount() const { return fFB.vblankCount; }
    void     addFramebufferVblank() { fFB.vblankCount++; }
    void     cleanupPhase2(void);

    // VRAM read/write from userspace (for GFX smoke test)
    IOReturn writeVRAM(uint64_t vramOff, const void *data, uint32_t size);
    IOReturn readVRAM(uint64_t vramOff, void *data, uint32_t size);
    uint32_t readCoreGET(void);
    void writeCorePUT(uint32_t dwordOffset);
    uint32_t getCorePbAddr(void) const { return fFB.corePbAddr; }
};

#endif /* GA104Device_hpp */
