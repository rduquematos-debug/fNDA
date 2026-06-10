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

IOReturn GSPProtocol::buildSetSystemInfo(GspRpcMessageHeader *msg,
                                          uint64_t bar0Phys, uint64_t bar1Phys,
                                          uint64_t bar2Phys, uint32_t deviceID,
                                          uint32_t subDeviceID, uint32_t revision,
                                          uint64_t nvDomainBusDeviceFunc)
{
    GspSystemInfo sysInfo;

    bzero(&sysInfo, sizeof(sysInfo));
    sysInfo.gpuPhysAddr = bar0Phys;
    sysInfo.gpuPhysFbAddr = bar1Phys;
    sysInfo.gpuPhysInstAddr = bar2Phys;
    sysInfo.nvDomainBusDeviceFunc = nvDomainBusDeviceFunc;
    sysInfo.maxUserVa = 0x7FFFFFFFFFFFULL;
    sysInfo.PCIDeviceID = deviceID;
    sysInfo.PCISubDeviceID = subDeviceID;
    sysInfo.PCIRevisionID = revision;
    sysInfo.hostPageSize = 0x1000; // 4K

    return buildMsg(msg, NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO,
                    sizeof(sysInfo), ++fSequence);
}

IOReturn GSPProtocol::buildSetRegistry(GspRpcMessageHeader *msg,
                                        uint32_t buf[], uint32_t *len)
{
    // Build PACKED_REGISTRY_TABLE with minimal entries
    struct regEntry {
        uint32_t keyLen;     // key string length including null
        uint32_t dataType;   // 1 = uint32
        uint32_t dataLen;    // 4
        char     key[32];    // key string
        uint32_t value;      // value
    } __attribute__((packed));

    struct {
        uint32_t size;
        uint32_t numEntries;
        struct regEntry entries[];
    } __attribute__((packed)) *reg;

    uint32_t totalSize = sizeof(uint32_t) * 2 + sizeof(struct regEntry) * 3;
    if (!buf || !len || *len < totalSize) return kIOReturnNoSpace;

    bzero(buf, totalSize);
    reg = (decltype(reg))buf;
    reg->numEntries = 3;

    // Entry 1: RMSecBusResetEnable = 1
    reg->entries[0].keyLen = 20;
    reg->entries[0].dataType = 1;
    reg->entries[0].dataLen = 4;
    memcpy(reg->entries[0].key, "RMSecBusResetEnable", 20);
    reg->entries[0].value = 1;

    // Entry 2: RMForcePcieConfigSave = 1
    reg->entries[1].keyLen = 23;
    reg->entries[1].dataType = 1;
    reg->entries[1].dataLen = 4;
    memcpy(reg->entries[1].key, "RMForcePcieConfigSave", 23);
    reg->entries[1].value = 1;

    // Entry 3: RMDevidCheckIgnore = 1
    reg->entries[2].keyLen = 19;
    reg->entries[2].dataType = 1;
    reg->entries[2].dataLen = 4;
    memcpy(reg->entries[2].key, "RMDevidCheckIgnore", 19);
    reg->entries[2].value = 1;

    reg->size = totalSize;
    *len = totalSize;

    return buildMsg(msg, NV_VGPU_MSG_FUNCTION_SET_REGISTRY,
                    totalSize, ++fSequence);
}

IOReturn GSPProtocol::buildGetStaticInfo(GspRpcMessageHeader *msg)
{
    return buildMsg(msg, 0x41, 0, ++fSequence);
}

IOReturn GSPProtocol::buildRmAlloc(GspRpcMessageHeader *msg,
                                    NvHandle hClient, NvHandle hParent,
                                    NvHandle hObject, uint32_t hClass,
                                    uint32_t paramsSize, const void *params)
{
    GspRmAllocParams *rm = (GspRmAllocParams*)msg->length; // FIXME: data follows header
    // Actually we need to build the payload after the header
    // For now, just build the RPC header + empty alloc params
    struct {
        NvHandle hClient;
        NvHandle hParent;
        NvHandle hObject;
        uint32_t hClass;
        uint32_t status;
        uint32_t paramsSize;
        uint32_t flags;
    } __attribute__((packed)) rpc;

    bzero(&rpc, sizeof(rpc));
    rpc.hClient = hClient;
    rpc.hParent = hParent;
    rpc.hObject = hObject;
    rpc.hClass = hClass;
    rpc.status = 0xFFFFFFFF;
    rpc.paramsSize = paramsSize;
    rpc.flags = 0;

    IOReturn ret = buildMsg(msg, NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC,
                            sizeof(rpc) + paramsSize, ++fSequence);
    if (ret != kIOReturnSuccess) return ret;

    // Copy alloc params after the RPC header
    uint8_t *rpcEnd = (uint8_t*)msg + sizeof(GspRpcMessageHeader);
    memcpy(rpcEnd, &rpc, sizeof(rpc));
    if (params && paramsSize > 0)
        memcpy(rpcEnd + sizeof(rpc), params, paramsSize);

    return kIOReturnSuccess;
}

IOReturn GSPProtocol::buildRmControl(GspRpcMessageHeader *msg,
                                      NvHandle hClient, NvHandle hObject,
                                      uint32_t cmd, uint32_t paramsSize,
                                      const void *params)
{
    struct {
        NvHandle hClient;
        NvHandle hObject;
        uint32_t cmd;
        uint32_t status;
        uint32_t paramsSize;
        uint32_t flags;
    } __attribute__((packed)) rpc;

    bzero(&rpc, sizeof(rpc));
    rpc.hClient = hClient;
    rpc.hObject = hObject;
    rpc.cmd = cmd;
    rpc.status = 0xFFFFFFFF;
    rpc.paramsSize = paramsSize;
    rpc.flags = 0;

    IOReturn ret = buildMsg(msg, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL,
                            sizeof(rpc) + paramsSize, ++fSequence);
    if (ret != kIOReturnSuccess) return ret;

    uint8_t *rpcEnd = (uint8_t*)msg + sizeof(GspRpcMessageHeader);
    memcpy(rpcEnd, &rpc, sizeof(rpc));
    if (params && paramsSize > 0)
        memcpy(rpcEnd + sizeof(rpc), params, paramsSize);

    return kIOReturnSuccess;
}

IOReturn GSPProtocol::buildDisplayGetNumHeads(GspRpcMessageHeader *msg,
                                               uint32_t subdev)
{
    uint32_t params[] = { subdev };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SYSTEM_GET_NUM_HEADS,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDisplayGetSupported(GspRpcMessageHeader *msg,
                                                uint32_t subdev)
{
    uint32_t params[] = { subdev };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildOrGetInfo(GspRpcMessageHeader *msg,
                                      uint32_t subdev, uint32_t displayId)
{
    uint32_t params[] = { subdev, displayId };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_SPECIFIC_OR_GET_INFO,
                          sizeof(params), params);
}

IOReturn GSPProtocol::buildDpAuxRead(GspRpcMessageHeader *msg,
                                      uint32_t subdev, uint32_t displayId,
                                      uint32_t addr, uint32_t size)
{
    uint32_t params[] = {
        subdev, displayId,
        0,       // bAddrOnly = 0
        0x09,    // cmd = DPCD READ
        addr,
        size - 1 // size encoded as bytes-1
    };
    return buildRmControl(msg, 0, subdev,
                          NV0073_CTRL_CMD_DP_AUXCH_CTRL,
                          sizeof(params), params);
}
