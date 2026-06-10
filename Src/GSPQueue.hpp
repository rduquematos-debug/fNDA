#ifndef GSPQueue_hpp
#define GSPQueue_hpp

#include <libkern/libkern.h>
#include <IOKit/IOService.h>
#include "GA104Regs.h"

// NVIDIA msgq library replica — exact same behavior as open-gpu-kernel-modules msgq.c
// Shared memory layout per queue:
//   [0..31]   TX header (msgqTxHeader, 8 x uint32)     — written by TX side
//   [32..35]  RX header (msgqRxHeader, 1 x uint32)     — written by RX side
//   [36..4095] padding
//   [4096..]  Ring buffer entries (msgCount * msgSize) 

class GSPQueue : public OSObject
{
    OSDeclareDefaultStructors(GSPQueue);

public:
    virtual bool init() override;
    virtual void free() override;

    // === msgqInit + msgqTxCreate (TX side — creates queue) ===
    // backing = shared memory buffer for this queue
    // size = total buffer size (e.g. 0x40000 = 256KB)
    // msgSize = entry size (e.g. 4096)
    // hdrAlign = header alignment exponent (e.g. 2 = 2^2 = 4 bytes)
    // entryAlign = entry alignment exponent (e.g. 12 = 2^12 = 4096 bytes)
    bool txCreate(void *backing, uint32_t size, uint32_t msgSize,
                  uint32_t hdrAlign, uint32_t entryAlign, uint32_t flags);

    // === msgqRxLink (RX side — links to existing TX) ===
    // Reads TX header from backing store, validates, writes readPtr=0
    bool rxLink(const void *backing, uint32_t size, uint32_t msgSize);

    // === TX: Get write buffer pointer ===
    uint8_t* getWriteBuffer(uint32_t index);

    // === TX: Submit buffers (advance writePtr with cyclic wrap) ===
    bool submitBuffers(uint32_t count);

    // === TX: Current write pointer ===
    uint32_t getWritePtr() const { return fTxWritePtr; }

    // === RX: Get number of available messages ===
    uint32_t getAvailable();

    // === RX: Get read buffer pointer ===
    const uint8_t* getReadBuffer(uint32_t index);

    // === RX: Mark messages as consumed ===
    bool markConsumed(uint32_t count);

    // === Accessors for header values (for RMARGS population) ===
    uint32_t getEntryOffset() const { return fEntryOff; }
    uint32_t getMsgCount() const { return fMsgCount; }
    uint32_t getMsgSize() const { return fMsgSize; }
    uint32_t getRxHdrOffset() const { return fRxHdrOff; }

private:
    // Shared memory pointers
    GspMsgqTxHeader *fTxHdr;          // TX header in shared memory
    volatile GspMsgqRxHeader *fRxHdr; // RX header in shared memory
    uint8_t           *fEntries;       // Ring buffer entries

    // Calculated layout
    uint32_t fSize;                    // Total buffer size
    uint32_t fMsgSize;                 // Entry size
    uint32_t fMsgCount;                // Number of entries
    uint32_t fRxHdrOff;                // RX header offset from buffer start
    uint32_t fEntryOff;                // Entry offset from buffer start

    // Internal state (mirrors NVIDIA msgqMetadata)
    uint32_t fTxWritePtr;              // Local write pointer
    uint32_t fTxFree;                  // Cached free space
    uint32_t fRxReadPtr;               // Local read pointer
    uint32_t fRxAvail;                 // Cached available count
    bool     fTxLinked;
    bool     fRxLinked;
};

#endif /* GSPQueue_hpp */