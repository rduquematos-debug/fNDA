#ifndef GSPProtocol_hpp
#define GSPProtocol_hpp

#include <libkern/libkern.h>
#include <IOKit/IOService.h>
#include "GA104Regs.h"

// RPC header version
#define GSP_RPC_HDR_VERSION             0x03000000
#define GSP_RPC_SIGNATURE               0x43505256  // 'RPCV'
#define GSP_RPC_ELEM_SIZE               0x1000
#define GSP_RPC_MAX_PAYLOAD             (GSP_RPC_ELEM_SIZE - sizeof(GspRpcMessageHeader))
#define GSP_RPC_POLL_TIMEOUT_MS         10000

// Reply policies
#define NVKM_GSP_RPC_REPLY_NOSEQ       0
#define NVKM_GSP_RPC_REPLY_RECV        1

// RM object alloc parameters
typedef struct {
    uint32_t hClass;
    uint32_t hObject;
    uint32_t status;
    uint32_t flags;
} __attribute__((packed)) RMAllocReply;

// NV0080 device alloc parameters (NV01_DEVICE_0)
typedef struct {
    uint32_t deviceId;
    uint32_t hClientShare;
    uint32_t flags;
} __attribute__((packed)) NV0080AllocParams;

// NV0073 display alloc parameters
typedef struct {
    uint32_t headMask;
    uint32_t sorMask;
    uint32_t numHeads;
    uint32_t numSors;
} __attribute__((packed)) NV0073AllocParams;

// Display modeset parameters
typedef struct {
    uint32_t headIndex;
    uint32_t sorIndex;
    uint32_t width;
    uint32_t height;
    uint32_t refreshHz;
    uint32_t bpp;
    uint32_t pitch;
    uint64_t framebufferAddr;
    uint32_t hTotal;
    uint32_t vTotal;
    uint32_t hSyncStart;
    uint32_t hSyncEnd;
    uint32_t vSyncStart;
    uint32_t vSyncEnd;
    uint32_t hBlankStart;
    uint32_t hBlankEnd;
    uint32_t vBlankStart;
    uint32_t vBlankEnd;
    uint32_t clockKHz;
    uint32_t colorFormat;
} __attribute__((packed)) GSPModesetParams;

// RM object handles (standard NVIDIA RM handles)
#define NVKM_RM_DEVICE                  0xDE1D0000
#define NVKM_RM_SUBDEVICE               0x5D1D0000
#define NVKM_RM_DISP                    0x00730000

// GSP RPC Protocol class
class GSPProtocol : public OSObject
{
    OSDeclareDefaultStructors(GSPProtocol);

public:
    virtual bool init() override;
    virtual void free() override;

    // Build RPC message in buffer
    IOReturn buildMsg(GspRpcMessageHeader *msg, uint32_t function,
                      uint32_t dataSize, uint32_t seqNum);

    // Pre-boot RPCs (queue to cmdq before CPUCTL=0x02)
    IOReturn buildSetSystemInfo(GspRpcMessageHeader *msg, 
                                uint64_t bar0Phys, uint64_t bar1Phys,
                                uint64_t bar2Phys, uint32_t deviceID,
                                uint32_t subDeviceID, uint32_t revision,
                                uint64_t nvDomainBusDeviceFunc);
    IOReturn buildSetRegistry(GspRpcMessageHeader *msg,
                              uint32_t buf[], uint32_t *len);

    // Post-init RPCs
    IOReturn buildGetStaticInfo(GspRpcMessageHeader *msg);
    IOReturn buildRmAlloc(GspRpcMessageHeader *msg, 
                          NvHandle hClient, NvHandle hParent,
                          NvHandle hObject, uint32_t hClass,
                          uint32_t paramsSize, const void *params);
    IOReturn buildRmControl(GspRpcMessageHeader *msg,
                            NvHandle hClient, NvHandle hObject,
                            uint32_t cmd, uint32_t paramsSize,
                            const void *params);

    // Convenience allocators (use default handles NVKM_RM_DEVICE, etc.)
    IOReturn buildAllocDevice(GspRpcMessageHeader *msg, NvHandle hClient,
                              NvHandle hDevice, uint32_t deviceId);
    IOReturn buildAllocSubdevice(GspRpcMessageHeader *msg, NvHandle hClient,
                                 NvHandle hDevice, NvHandle hSubdevice);
    IOReturn buildAllocDisp(GspRpcMessageHeader *msg, NvHandle hClient,
                            NvHandle hSubdevice, NvHandle hDisp,
                            uint32_t headMask, uint32_t sorMask);

    // Display RPCs
    IOReturn buildDisplayGetNumHeads(GspRpcMessageHeader *msg, uint32_t subdev);
    IOReturn buildDisplayGetSupported(GspRpcMessageHeader *msg, uint32_t subdev);
    IOReturn buildOrGetInfo(GspRpcMessageHeader *msg, uint32_t subdev, uint32_t displayId);
    IOReturn buildOrAssign(GspRpcMessageHeader *msg, uint32_t subdev, uint32_t displayId,
                           uint32_t sorExcludeMask, uint32_t protocol);
    IOReturn buildDpAuxRead(GspRpcMessageHeader *msg, uint32_t subdev,
                            uint32_t displayId, uint32_t addr, uint32_t size);
    IOReturn buildDpLinkTrain(GspRpcMessageHeader *msg, uint32_t subdev,
                              uint32_t displayId, uint32_t laneCount,
                              uint32_t linkBw);
    IOReturn buildDpConfigStream(GspRpcMessageHeader *msg, uint32_t subdev,
                                 uint32_t head, uint32_t sor,
                                 const GSPModesetParams *params);
    IOReturn buildDfpGetAttachedIds(GspRpcMessageHeader *msg, uint32_t subdev);
    IOReturn buildDfpGetInfo(GspRpcMessageHeader *msg, uint32_t subdev, uint32_t displayId);
    IOReturn buildDpLinkConfig(GspRpcMessageHeader *msg, uint32_t subdev,
                               uint32_t displayId, uint32_t laneCount,
                               uint32_t linkBw, uint32_t postCursor, uint32_t preEmphasis);
    IOReturn buildHeadSetControl(GspRpcMessageHeader *msg, uint32_t subdev,
                                 uint32_t head, uint32_t sor, uint32_t protocol);
    IOReturn buildHeadSetTimings(GspRpcMessageHeader *msg, uint32_t subdev,
                                 uint32_t head, const GSPModesetParams *params);
    IOReturn buildFlip(GspRpcMessageHeader *msg, uint32_t subdev,
                       uint32_t head, uint64_t surfaceAddr, uint32_t pitch);

private:
    uint32_t fSequence;
};

#endif /* GSPProtocol_hpp */
