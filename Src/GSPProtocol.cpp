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
