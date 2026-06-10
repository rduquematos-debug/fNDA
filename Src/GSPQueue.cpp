#include "GSPQueue.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <kern/debug.h>

#define super OSObject
OSDefineMetaClassAndStructors(GSPQueue, OSObject);

bool GSPQueue::init()
{
    if (!super::init()) return false;
    fTxHdr = nullptr;
    fRxHdr = nullptr;
    fEntries = nullptr;
    fSize = 0;
    fMsgSize = 0;
    fMsgCount = 0;
    fRxHdrOff = 0;
    fEntryOff = 0;
    fTxWritePtr = 0;
    fTxFree = 0;
    fRxReadPtr = 0;
    fRxAvail = 0;
    fTxLinked = false;
    fRxLinked = false;
    return true;
}

void GSPQueue::free()
{
    super::free();
}

// === msgqTxCreate — NVIDIA msgq.c exact replica ===
// Calculates rxHdrOff, entryOff, msgCount. Writes TX header to backing store.
bool GSPQueue::txCreate(void *backing, uint32_t size, uint32_t msgSize,
                         uint32_t hdrAlign, uint32_t entryAlign, uint32_t flags)
{
    if (!backing || fTxLinked) return false;
    if (msgSize < NVG_MSGQ_MSG_SIZE_MIN || msgSize > size) return false;
    if (hdrAlign < NVG_MSGQ_MIN_ALIGN || hdrAlign > NVG_MSGQ_MAX_ALIGN) return false;
    if (entryAlign < NVG_MSGQ_MIN_ALIGN || entryAlign > NVG_MSGQ_MAX_ALIGN) return false;
    if ((uintptr_t)backing & ((1 << hdrAlign) - 1)) return false;

    // Calculate offsets (NVIDIA msgq.c lines 236-238)
    fRxHdrOff = (sizeof(GspMsgqTxHeader) + (1 << hdrAlign) - 1) & ~((1 << hdrAlign) - 1);
    fEntryOff = (fRxHdrOff + sizeof(GspMsgqRxHeader) + (1 << entryAlign) - 1) & ~((1 << entryAlign) - 1);

    if (size < (fEntryOff + msgSize)) return false;

    // Fill local copy of TX header
    fSize = size;
    fMsgSize = msgSize;
    fMsgCount = (size - fEntryOff) / msgSize;
    fTxWritePtr = 0;
    fTxHdr = (GspMsgqTxHeader*)backing;
    fRxHdr = (GspMsgqRxHeader*)((uint8_t*)backing + fRxHdrOff);
    fEntries = (uint8_t*)backing + fEntryOff;
    
    // Shared buffer TX header initialization (NVIDIA writes via memcpy to backend)
    bzero(fTxHdr, sizeof(GspMsgqTxHeader));
    fTxHdr->version  = NVG_MSGQ_VERSION;
    fTxHdr->size     = size;
    fTxHdr->msgSize  = msgSize;
    fTxHdr->msgCount = fMsgCount;
    fTxHdr->writePtr = 0;
    fTxHdr->flags    = flags;
    fTxHdr->rxHdrOff = fRxHdrOff;
    fTxHdr->entryOff = fEntryOff;
    __sync_synchronize();

    fTxLinked = true;
    fTxFree = fMsgCount - 1;  // NVIDIA: allow adding before rx is linked
    return true;
}

// === msgqRxLink — NVIDIA msgq.c exact replica ===
// Reads remote TX header, validates, writes our readPtr=0
bool GSPQueue::rxLink(const void *backing, uint32_t size, uint32_t msgSize)
{
    if (!backing || fRxLinked) return false;
    if (msgSize < NVG_MSGQ_MSG_SIZE_MIN || msgSize > size) return false;

    // Copy remote TX header
    const GspMsgqTxHeader *remoteTx = (const GspMsgqTxHeader*)backing;
    GspMsgqTxHeader rx;
    memcpy(&rx, remoteTx, sizeof(rx));
    __sync_synchronize();

    // Validate — NVIDIA msgqRxLink checks (lines 386-405)
    if (rx.version != NVG_MSGQ_VERSION) return false;
    if (rx.size != size) return false;
    if (rx.msgSize != msgSize) return false;
    if (rx.rxHdrOff < sizeof(GspMsgqTxHeader)) return false;
    if (rx.entryOff < rx.rxHdrOff + sizeof(GspMsgqRxHeader)) return false;
    uint32_t expectedCount = (size - rx.entryOff) / msgSize;
    if (rx.msgCount != expectedCount) return false;

    // Save remote info (for read operations)
    fMsgCount = rx.msgCount;
    fMsgSize = rx.msgSize;
    fEntryOff = rx.entryOff;
    fRxHdrOff = rx.rxHdrOff;

    // Point to their entries
    fEntries = (uint8_t*)((uintptr_t)backing + fEntryOff);
    
    // Write readPtr=0 to signal RX ready — NVIDIA line 436
    fRxReadPtr = 0;
    volatile uint32_t *pReadOut = (volatile uint32_t*)((uint8_t*)backing + fRxHdrOff);
    *pReadOut = 0;
    __sync_synchronize();

    fRxLinked = true;
    return true;
}

// === msgqTxGetWriteBuffer — NVIDIA msgq.c lines 501-531 ===
uint8_t* GSPQueue::getWriteBuffer(uint32_t index)
{
    if (!fTxLinked) return NULL;
    // Check free space
    if (index >= fTxFree) {
        // Recalculate free space from remote readPtr (NVIDIA msgqTxGetFreeSpace)
        if (fRxHdr) {
            uint32_t remoteRead = fRxHdr->readPtr;
            if (remoteRead >= fMsgCount) return NULL;
            fTxFree = remoteRead + fMsgCount - fTxWritePtr - 1;
            if (fTxFree >= fMsgCount) fTxFree -= fMsgCount;
        }
        if (index >= fTxFree) return NULL;
    }

    uint32_t wp = fTxWritePtr + index;
    if (wp >= fMsgCount) wp -= fMsgCount;
    if (wp >= fMsgCount) return NULL;

    return fEntries + (wp * fMsgSize);
}

// === msgqTxSubmitBuffers — NVIDIA msgq.c lines 533-600 ===
bool GSPQueue::submitBuffers(uint32_t count)
{
    if (!fTxLinked) return false;
    if (!fTxHdr) return false;

    // Advance write pointer with cyclic wrap (NVIDIA lines 558-562)
    fTxWritePtr += count;
    if (fTxWritePtr >= fMsgCount) fTxWritePtr -= fMsgCount;

    // Write to shared memory (NVIDIA _backendWrite32 via pWriteOutgoing)
    fTxHdr->writePtr = fTxWritePtr;
    __sync_synchronize();

    // Update cached free space
    fTxFree -= count;

    return true;
}

// === msgqRxGetReadAvailable — NVIDIA msgq.c lines 640-669 ===
uint32_t GSPQueue::getAvailable()
{
    if (!fRxLinked || !fTxHdr) return 0;

    // Read remote write pointer
    uint32_t remoteWp = fTxHdr->writePtr;
    if (remoteWp >= fMsgCount) return 0;

    // Calculate available (NVIDIA line 660-666)
    fRxAvail = remoteWp + fMsgCount - fRxReadPtr;
    if (fRxAvail >= fMsgCount) fRxAvail -= fMsgCount;

    return fRxAvail;
}

// === msgqRxGetReadBuffer — NVIDIA msgq.c lines 672-701 ===
const uint8_t* GSPQueue::getReadBuffer(uint32_t index)
{
    if (!fRxLinked) return NULL;

    // Check if enough available (NVIDIA line 688-692)
    if (index >= fRxAvail && index >= getAvailable()) return NULL;

    uint32_t rp = fRxReadPtr + index;
    if (rp >= fMsgCount) rp -= fMsgCount;

    return fEntries + (rp * fMsgSize);
}

// === msgqRxMarkConsumed — NVIDIA msgq.c lines 703-764 ===
bool GSPQueue::markConsumed(uint32_t count)
{
    if (!fRxLinked) return false;

    // Advance read pointer with wrap (NVIDIA lines 721-725)
    fRxReadPtr += count;
    if (fRxReadPtr >= fMsgCount) fRxReadPtr -= fMsgCount;

    // Write to shared memory RX header (NVIDIA _backendWrite32 via pReadOutgoing)
    if (fRxHdr) {
        fRxHdr->readPtr = fRxReadPtr;
        __sync_synchronize();
    }

    fRxAvail -= count;
    return true;
}