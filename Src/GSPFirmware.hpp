#ifndef GSPFirmware_hpp
#define GSPFirmware_hpp

#include <libkern/libkern.h>
#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#define GSP_FW_MAGIC           0x3B1A4E55
#define GSP_FW_MAX_SIZE        (96 * 1024 * 1024)
#define GSP_FW_HEADER_SIZE     256
#define GSP_FW_SIGNATURE_SIZE  256
#define GSP_FW_BOOT_TIMEOUT    5000
#define GSP_BOOT_MAGIC         0xFEEDA55A

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t headerSize;
    uint32_t totalSize;
    uint32_t codeOffset;
    uint32_t codeSize;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t bootloaderOffset;
    uint32_t bootloaderSize;
    uint32_t reserved[10];
    uint8_t  signature[GSP_FW_SIGNATURE_SIZE];
} GSPFirmwareHeader;

typedef struct {
    uint32_t bootMagic;
    uint32_t bootStatus;
    uint32_t fwVersion;
    uint32_t capabilities;
    uint32_t maxQueueSize;
    uint32_t maxCommands;
    uint32_t reserved[10];
    uint64_t bootTimestamp;
} GSPBootInfo;

class GSPFirmware : public OSObject
{
    OSDeclareDefaultStructors(GSPFirmware);

public:
    virtual bool init() override;
    virtual void free() override;

    IOReturn loadFromData(void *data, uint32_t size);
    IOReturn loadExternal(void *data, uint32_t size);
    IOReturn upload(uint8_t *destination, uint32_t maxSize);
    IOReturn boot(void);

    bool isLoaded() const { return fLoaded; }
    bool isBooted() const { return fBooted; }
    uint32_t getVersion() const { return fVersion; }
    GSPBootInfo *getBootInfo() { return &fBootInfo; }

    uint8_t *getFirmwareData() { return fFirmwareData; }
    uint32_t getFirmwareSize() const { return fFirmwareSize; }

    uint8_t *getFWImage(uint32_t *outSize);
    uint32_t getFWImageOffset() const { return fFWImageOffset; }
    uint32_t getFWImageSize() const { return fFWImageSize; }

private:
    IOBufferMemoryDescriptor *fFirmwareBuffer;
    uint8_t     *fFirmwareData;
    uint32_t     fFirmwareSize;
    bool         fLoaded;
    bool         fBooted;
    uint32_t     fVersion;
    GSPBootInfo  fBootInfo;

    uint32_t     fSectionOffset;
    uint16_t     fSectionCount;
    uint16_t     fSectionEntrySize;
    uint32_t     fFWImageOffset;
    uint32_t     fFWImageSize;

    bool parseHeader(void);
    bool parseSections(void);
    bool verifySignature(void);
};

#endif /* GSPFirmware_hpp */
