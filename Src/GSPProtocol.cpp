#include "GSPProtocol.hpp"
#include <IOKit/IOLib.h>
#include <string.h>

#define super OSObject
OSDefineMetaClassAndStructors(GSPProtocol, OSObject);

bool GSPProtocol::init()
{
    if (!super::init()) return false;
    fSequence = 0;
    return true;
}

void GSPProtocol::free()
{
    super::free();
}

IOReturn GSPProtocol::buildMsg(GspRpcMessageHeader *msg, uint32_t function,
                                uint32_t dataSize, uint32_t seqNum)
{
    if (!msg) return kIOReturnBadArgument;

    bzero(msg, sizeof(GspRpcMessageHeader));
    msg->signature = GSP_RPC_SIGNATURE;       // MUST be first field!
    msg->headerVersion = GSP_RPC_HDR_VERSION;
    msg->length = sizeof(GspRpcMessageHeader) + dataSize;
    msg->function = function;
    msg->rpcResult = 0xFFFFFFFF; // pending
    msg->rpcResultPrivate = 0;
    msg->sequence = seqNum;
    msg->spare = 0;
    return kIOReturnSuccess;
}

IOReturn GSPProtocol::buildAllocDevice(GspRpcMessageHeader *msg,
                                        NvHandle hClient,
                                        NvHandle hDevice, uint32_t deviceId)
{
    NV0080AllocParams params;
    bzero(&params, sizeof(params));
    params.deviceId = deviceId;
    params.hClientShare = hClient;
    params.flags = 0;
    return buildRmAlloc(msg, hClient, hClient, hDevice,
                        NV01_DEVICE_0, sizeof(params), &params);
}

IOReturn GSPProtocol::buildAllocSubdevice(GspRpcMessageHeader *msg,
                                           NvHandle hClient,
                                           NvHandle hDevice,
                                           NvHandle hSubdevice)
{
    uint32_t index = 0;
    return buildRmAlloc(msg, hClient, hDevice, hSubdevice,
                        0x20800000 | index, sizeof(index), &index);
}

IOReturn GSPProtocol::buildAllocDisp(GspRpcMessageHeader *msg,
                                      NvHandle hClient,
                                      NvHandle hSubdevice,
                                      NvHandle hDisp,
                                      uint32_t headMask, uint32_t sorMask)
{
    NV0073AllocParams params;
    bzero(&params, sizeof(params));
    params.headMask = headMask;
    params.sorMask = sorMask;
    params.numHeads = 4;
    params.numSors = 4;
    return buildRmAlloc(msg, hClient, hSubdevice, hDisp,
                        0x00730000, sizeof(params), &params);
}

IOReturn GSPProtocol::buildDpLinkConfig(GspRpcMessageHeader *msg,
                                         uint32_t subdev, uint32_t displayId,
                                         uint32_t laneCount, uint32_t linkBw,
                                         uint32_t postCursor, uint32_t preEmphasis)
{
    // NV0073_CTRL_CMD_DP_SET_LINK_CONFIG
    uint32_t params[] = { subdev, displayId, laneCount, linkBw, postCursor, preEmphasis };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DP_SET_LINK_CONFIG,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildHeadSetControl(GspRpcMessageHeader *msg,
                                           uint32_t subdev, uint32_t head,
                                           uint32_t sor, uint32_t protocol)
{
    // NV0073_CTRL_CMD_HEAD_SET_CONTROL
    // Configures head -> SOR routing and protocol (TMDS/DP)
    uint32_t params[] = { subdev, head, sor, protocol, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_HEAD_SET_CONTROL,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildHeadSetTimings(GspRpcMessageHeader *msg,
                                           uint32_t subdev, uint32_t head,
                                           const GSPModesetParams *params)
{
    // NV0073_CTRL_CMD_HEAD_SET_TIMING
    // Set raster timings for a head (resolution, sync, blank)
    if (!params) return kIOReturnBadArgument;
    uint32_t buf[32];
    bzero(buf, sizeof(buf));
    buf[0] = subdev;
    buf[1] = head;
    memcpy(&buf[2], params, sizeof(GSPModesetParams));
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_HEAD_SET_TIMING,
                          sizeof(buf), buf);
}

IOReturn GSPProtocol::buildFlip(GspRpcMessageHeader *msg,
                                 uint32_t subdev, uint32_t head,
                                 uint64_t surfaceAddr, uint32_t pitch)
{
    // NV0073_CTRL_CMD_FLIP
    // Flip to a new scanout surface
    uint32_t params[] = {
        subdev, head,
        (uint32_t)(surfaceAddr & 0xFFFFFFFF),
        (uint32_t)(surfaceAddr >> 32),
        pitch, 0
    };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_FLIP,
                          sizeof(params), params);
}

// ----------------------------------------------------------------
// Foundation: buildRmAlloc / buildRmControl (called by all others)
// ----------------------------------------------------------------

IOReturn GSPProtocol::buildRmAlloc(GspRpcMessageHeader *msg,
                                    NvHandle hClient, NvHandle hParent,
                                    NvHandle hObject, uint32_t hClass,
                                    uint32_t paramsSize, const void *params)
{
    uint32_t totalSize = sizeof(GspRmAllocParams) + paramsSize;
    IOReturn ret = buildMsg(msg, NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC,
                            totalSize, fSequence++);
    if (ret != kIOReturnSuccess) return ret;

    GspRmAllocParams *rm = (GspRmAllocParams*)((uint8_t*)msg + sizeof(GspRpcMessageHeader));
    bzero(rm, sizeof(GspRmAllocParams));
    rm->hClient = hClient;
    rm->hParent = hParent;
    rm->hObject = hObject;
    rm->hClass  = hClass;
    rm->paramsSize = paramsSize;
    rm->status  = 0;
    rm->flags   = 0;
    if (params && paramsSize > 0)
        memcpy(rm->params, params, paramsSize);
    return kIOReturnSuccess;
}

IOReturn GSPProtocol::buildRmControl(GspRpcMessageHeader *msg,
                                      NvHandle hClient, NvHandle hObject,
                                      uint32_t cmd, uint32_t paramsSize,
                                      const void *params)
{
    uint32_t totalSize = sizeof(GspRmControlParams) + paramsSize;
    IOReturn ret = buildMsg(msg, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
                            totalSize, fSequence++);
    if (ret != kIOReturnSuccess) return ret;

    GspRmControlParams *rm = (GspRmControlParams*)((uint8_t*)msg + sizeof(GspRpcMessageHeader));
    bzero(rm, sizeof(GspRmControlParams));
    rm->hClient = hClient;
    rm->hObject = hObject;
    rm->cmd     = cmd;
    rm->paramsSize = paramsSize;
    rm->status  = 0;
    rm->flags   = 0;
    if (params && paramsSize > 0)
        memcpy(rm->params, params, paramsSize);
    return kIOReturnSuccess;
}

// ----------------------------------------------------------------
// Pre-boot RPCs
// ----------------------------------------------------------------

IOReturn GSPProtocol::buildSetSystemInfo(GspRpcMessageHeader *msg,
                                          uint64_t bar0Phys, uint64_t bar1Phys,
                                          uint64_t bar2Phys, uint32_t deviceID,
                                          uint32_t subDeviceID, uint32_t revision,
                                          uint64_t nvDomainBusDeviceFunc)
{
    uint32_t params[16];
    bzero(params, sizeof(params));
    params[0]  = (uint32_t)(bar0Phys & 0xFFFFFFFF);
    params[1]  = (uint32_t)(bar0Phys >> 32);
    params[2]  = (uint32_t)(bar1Phys & 0xFFFFFFFF);
    params[3]  = (uint32_t)(bar1Phys >> 32);
    params[4]  = (uint32_t)(bar2Phys & 0xFFFFFFFF);
    params[5]  = (uint32_t)(bar2Phys >> 32);
    params[6]  = deviceID;
    params[7]  = subDeviceID;
    params[8]  = revision;
    params[9]  = (uint32_t)(nvDomainBusDeviceFunc & 0xFFFFFFFF);
    params[10] = (uint32_t)(nvDomainBusDeviceFunc >> 32);
    return buildRmControl(msg, 0, 0,
                          NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildSetRegistry(GspRpcMessageHeader *msg,
                                        uint32_t buf[], uint32_t *len)
{
    if (!buf || !len || *len == 0) return kIOReturnBadArgument;
    // Registry entries: [keyLen, keyData..., valLen, valData...]
    // For now: pass through with minimal header
    uint32_t totalLen = *len;
    IOReturn ret = buildMsg(msg, 0x00730004, totalLen, fSequence++);
    if (ret != kIOReturnSuccess) return ret;
    memcpy((uint8_t*)msg + sizeof(GspRpcMessageHeader), buf, totalLen);
    return kIOReturnSuccess;
}

// ----------------------------------------------------------------
// Post-init RPCs
// ----------------------------------------------------------------

IOReturn GSPProtocol::buildGetStaticInfo(GspRpcMessageHeader *msg)
{
    uint32_t flags = 0;
    return buildRmControl(msg, 0, 0, 0x00730002, sizeof(flags), &flags);
}

// ----------------------------------------------------------------
// Display RPCs
// ----------------------------------------------------------------

IOReturn GSPProtocol::buildDisplayGetNumHeads(GspRpcMessageHeader *msg,
                                               uint32_t subdev)
{
    uint32_t params[] = { subdev, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SYSTEM_GET_NUM_HEADS,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDisplayGetSupported(GspRpcMessageHeader *msg,
                                                uint32_t subdev)
{
    uint32_t params[] = { subdev, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildOrGetInfo(GspRpcMessageHeader *msg,
                                      uint32_t subdev, uint32_t displayId)
{
    uint32_t params[] = { subdev, displayId, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SPECIFIC_OR_GET_INFO,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildOrAssign(GspRpcMessageHeader *msg,
                                     uint32_t subdev, uint32_t displayId,
                                     uint32_t sorExcludeMask, uint32_t protocol)
{
    uint32_t params[] = { subdev, displayId, sorExcludeMask, protocol, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DFP_ASSIGN_SOR,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDpAuxRead(GspRpcMessageHeader *msg,
                                      uint32_t subdev, uint32_t displayId,
                                      uint32_t addr, uint32_t size)
{
    uint32_t params[] = { subdev, displayId, addr, size, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DP_AUXCH_CTRL,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDpLinkTrain(GspRpcMessageHeader *msg,
                                        uint32_t subdev, uint32_t displayId,
                                        uint32_t laneCount, uint32_t linkBw)
{
    uint32_t params[] = { subdev, displayId, laneCount, linkBw, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DP_CTRL,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDpConfigStream(GspRpcMessageHeader *msg,
                                           uint32_t subdev, uint32_t head,
                                           uint32_t sor,
                                           const GSPModesetParams *params)
{
    if (!params) return kIOReturnBadArgument;
    uint32_t buf[32];
    bzero(buf, sizeof(buf));
    buf[0] = subdev;
    buf[1] = head;
    buf[2] = sor;
    memcpy(&buf[3], params, sizeof(GSPModesetParams));
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DP_CONFIG_STREAM,
                          sizeof(buf), buf);
}

IOReturn GSPProtocol::buildDfpGetAttachedIds(GspRpcMessageHeader *msg,
                                              uint32_t subdev)
{
    uint32_t params[] = { subdev, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DFP_GET_ATTACHED_IDS,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDfpGetInfo(GspRpcMessageHeader *msg,
                                       uint32_t subdev, uint32_t displayId)
{
    uint32_t params[] = { subdev, displayId, 0 };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DFP_GET_INFO,
                          sizeof(params), params);
}
