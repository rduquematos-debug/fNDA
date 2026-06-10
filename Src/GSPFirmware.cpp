#include "GSPFirmware.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>

#define super OSObject
OSDefineMetaClassAndStructors(GSPFirmware, OSObject);

bool GSPFirmware::init()
{
    if (!super::init()) return false;

    fFirmwareBuffer = nullptr;
    fFirmwareData = nullptr;
    fFirmwareSize = 0;
    fLoaded = false;
    fBooted = false;
    fVersion = 0;
    memset(&fBootInfo, 0, sizeof(fBootInfo));

    fSectionOffset = 0;
    fSectionCount = 0;
    fSectionEntrySize = 0;
    fFWImageOffset = 0;
    fFWImageSize = 0;

    return true;
}

void GSPFirmware::free()
{
    if (fFirmwareBuffer) {
        fFirmwareBuffer->complete();
        fFirmwareBuffer->release();
        fFirmwareBuffer = nullptr;
    }
    fFirmwareData = nullptr;
    fFirmwareSize = 0;

    super::free();
}

IOReturn GSPFirmware::loadFromData(void *data, uint32_t size)
{
    if (!data || size == 0) return kIOReturnBadArgument;
    if (size > GSP_FW_MAX_SIZE) {
        IOLog("GSP: Firmware too large (%u bytes)\n", size);
        return kIOReturnNoSpace;
    }

    IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithOptions(
        kernel_task,
        kIODirectionInOut,
        size,
        page_size);

    if (!buf) {
        IOLog("GSP: Failed to allocate firmware buffer (%u bytes)\n", size);
        return kIOReturnNoMemory;
    }

    buf->prepare();
    uint8_t *fwData = reinterpret_cast<uint8_t*>(buf->getBytesNoCopy());
    if (!fwData) {
        buf->release();
        return kIOReturnNoMemory;
    }

    memcpy(fwData, data, size);
    fFirmwareBuffer = buf;
    fFirmwareData = fwData;
    fFirmwareSize = size;

    if (!parseHeader()) {
        buf->complete();
        buf->release();
        fFirmwareBuffer = nullptr;
        fFirmwareData = nullptr;
        fFirmwareSize = 0;
        IOLog("GSP: Invalid firmware header\n");
        return kIOReturnUnsupported;
    }

    IOLog("GSP: Firmware loaded (%u bytes, version %u)\n", size, fVersion);
    fLoaded = true;

    return kIOReturnSuccess;
}

IOReturn GSPFirmware::loadExternal(void *data, uint32_t size)
{
    if (!data || size == 0) return kIOReturnBadArgument;
    if (size > GSP_FW_MAX_SIZE) {
        IOLog("GSP: Firmware too large (%u)\n", size);
        return kIOReturnNoSpace;
    }
    fFirmwareData = (uint8_t*)data;
    fFirmwareSize = size;
    fFirmwareBuffer = nullptr;
    if (!parseHeader()) {
        fFirmwareData = nullptr;
        fFirmwareSize = 0;
        return kIOReturnUnsupported;
    }
    fLoaded = true;
    IOLog("GSP: External FW loaded (%u bytes)\n", size);
    return kIOReturnSuccess;
}

IOReturn GSPFirmware::upload(uint8_t *destination, uint32_t maxSize)
{
    if (!fLoaded || !fFirmwareData) {
        return kIOReturnNotReady;
    }

    uint32_t imgSize = 0;
    uint8_t *imgData = getFWImage(&imgSize);
    if (!imgData || imgSize == 0) {
        IOLog("GSP: No .fwimage section available\n");
        return kIOReturnUnsupported;
    }

    if (imgSize > maxSize) {
        IOLog("GSP: Destination too small (%u < %u)\n",
              maxSize, imgSize);
        return kIOReturnNoSpace;
    }

    memcpy(destination, imgData, imgSize);

    IOLog("GSP: .fwimage uploaded to GPU memory (%u bytes at %p)\n",
          imgSize, destination);

    return kIOReturnSuccess;
}

IOReturn GSPFirmware::boot()
{
    if (!fLoaded) return kIOReturnNotReady;

    IOLog("GSP: Firmware %u bytes ready for boot\n", fFirmwareSize);
    fBooted = false;

    return kIOReturnSuccess;
}

bool GSPFirmware::parseHeader()
{
    if (!fFirmwareData || fFirmwareSize < 64) {
        return false;
    }

    const uint8_t *bytes = reinterpret_cast<const uint8_t*>(fFirmwareData);

    uint32_t magic = *(const uint32_t*)(bytes + 0);
    if (magic != 0x464C457F) {
        IOLog("GSP: Not an ELF file (magic: 0x%08x, expected 0x7F454C46)\n", magic);
        return false;
    }

    uint8_t  elfClass = bytes[4];
    uint16_t machine  = *(const uint16_t*)(bytes + 0x12);

    if (elfClass != 2 || machine != 0xF3) {
        IOLog("GSP: Unexpected ELF format (class=%u, machine=0x%04x)\n",
              elfClass, machine);
        return false;
    }

    fSectionOffset = *(const uint32_t*)(bytes + 0x28);
    fSectionEntrySize = *(const uint16_t*)(bytes + 0x3A);
    fSectionCount  = *(const uint16_t*)(bytes + 0x3C);

    IOLog("GSP: ELF64 RISC-V firmware (%u bytes, %u sections)\n",
          fFirmwareSize, fSectionCount);
    fVersion = 1;

    if (!parseSections()) {
        IOLog("GSP: Failed to parse ELF sections (no .fwimage found)\n");
        return false;
    }

    IOLog("GSP: .fwimage at offset 0x%x size 0x%x (%u bytes)\n",
          fFWImageOffset, fFWImageSize, fFWImageSize);

    return true;
}

bool GSPFirmware::parseSections()
{
    if (!fFirmwareData || fFirmwareSize < 64 || !fSectionOffset || !fSectionCount) {
        return false;
    }

    if (fSectionEntrySize < 64) {
        IOLog("GSP: Invalid section header entry size %u\n", fSectionEntrySize);
        return false;
    }

    const uint8_t *bytes = reinterpret_cast<const uint8_t*>(fFirmwareData);
    uint16_t shstrndx = *(const uint16_t*)(bytes + 0x3E);

    if (shstrndx >= fSectionCount) {
        IOLog("GSP: Invalid string table index %u\n", shstrndx);
        return false;
    }

    uint32_t strTabOffset = fSectionOffset + shstrndx * fSectionEntrySize;
    if (strTabOffset + 64 > fFirmwareSize) return false;

    const uint8_t *strHdr = bytes + strTabOffset;
    uint32_t strDataOffset = *(const uint32_t*)(strHdr + 0x18);
    uint64_t strDataSize   = *(const uint64_t*)(strHdr + 0x20);

    if (strDataOffset + strDataSize > fFirmwareSize) return false;
    const char *strTab = reinterpret_cast<const char*>(bytes + strDataOffset);

    for (uint16_t i = 0; i < fSectionCount; i++) {
        uint32_t shdrOff = fSectionOffset + i * fSectionEntrySize;
        if (shdrOff + 64 > fFirmwareSize) return false;

        const uint8_t *shdr = bytes + shdrOff;
        uint32_t shName = *(const uint32_t*)shdr;
        uint32_t shOffset = *(const uint32_t*)(shdr + 0x18);
        uint64_t shSize   = *(const uint64_t*)(shdr + 0x20);

        if (shName >= strDataSize) continue;
        const char *secName = strTab + shName;

        if (strcmp(secName, ".fwimage") == 0) {
            if (shOffset + shSize > fFirmwareSize) {
                IOLog("GSP: .fwimage extends past file end\n");
                return false;
            }
            fFWImageOffset = shOffset;
            fFWImageSize = (uint32_t)shSize;
            return true;
        }
    }

    IOLog("GSP: .fwimage section not found\n");
    return false;
}

uint8_t* GSPFirmware::getFWImage(uint32_t *outSize)
{
    if (!fLoaded || !fFWImageOffset || !fFWImageSize) {
        if (outSize) *outSize = 0;
        return nullptr;
    }
    if (outSize) *outSize = fFWImageSize;
    return fFirmwareData + fFWImageOffset;
}

bool GSPFirmware::verifySignature()
{
    return true;
}
