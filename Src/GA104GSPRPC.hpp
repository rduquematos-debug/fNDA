// GA104GSPRPC.hpp — GSP RPC/queue communication
#ifndef GA104GSPRPC_hpp
#define GA104GSPRPC_hpp

#include "GA104Device.hpp"

class GA104GSPRPC : public OSObject {
    OSDeclareDefaultStructors(GA104GSPRPC);
    
public:
    virtual bool init(GA104Device *device) override;
    virtual void free() override;
    
    IOReturn sendGspRpc(GspRpcMessageHeader *msg, void *payload,
                        uint32_t payloadSize,
                        GspRpcMessageHeader *reply, uint32_t replyMaxSize,
                        uint32_t *replySize, uint32_t timeoutMs);
    IOReturn pollMsgq(uint32_t targetSeq, uint32_t targetFunc,
                      GspRpcMessageHeader *reply, uint32_t replyMaxSize,
                      uint32_t *replySize, uint32_t timeoutMs);
    
private:
    GA104Device *fDevice;
};

#endif
