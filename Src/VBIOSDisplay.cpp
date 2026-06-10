#include "VBIOSDisplay.hpp"
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(VBIOSDisplay, OSObject);

bool VBIOSDisplay::init()
{
    if (!super::init()) return false;

    fVBIOSData = nullptr;
    fVBIOSSize = 0;
    fConnectorCount = 0;
    fDcbVersion = 0;
    fDcbHdr = 0;
    fDcbCount = 0;
    fDcbEntryLen = 0;

    memset(&fCurrentMode, 0, sizeof(fCurrentMode));
    memset(fConnectors, 0, sizeof(fConnectors));

    return true;
}

void VBIOSDisplay::free()
{
    super::free();
}

uint8_t VBIOSDisplay::readU8(uint32_t offset)
{
    if (!fVBIOSData || offset >= fVBIOSSize) return 0;
    return fVBIOSData[offset];
}

uint16_t VBIOSDisplay::readU16(uint32_t offset)
{
    if (!fVBIOSData || offset + 1 >= fVBIOSSize) return 0;
    return fVBIOSData[offset] | (fVBIOSData[offset + 1] << 8);
}

uint32_t VBIOSDisplay::readU32(uint32_t offset)
{
    if (!fVBIOSData || offset + 3 >= fVBIOSSize) return 0;
    return fVBIOSData[offset]
         | (fVBIOSData[offset + 1] << 8)
         | (fVBIOSData[offset + 2] << 16)
         | (fVBIOSData[offset + 3] << 24);
}

IOReturn VBIOSDisplay::parseVBIOS(uint8_t *vbiosData, uint32_t size)
{
    if (!vbiosData || size < 256) return kIOReturnBadArgument;

    fVBIOSData = vbiosData;
    fVBIOSSize = size;

    IOLog("VBIOS: Parsing %u bytes\n", size);

    scanPCIR();

    IOReturn ret = parseDCB();
    if (ret != kIOReturnSuccess) {
        IOLog("VBIOS: DCB parse failed (%d)\n", ret);
        return ret;
    }

    ret = parseCONN();
    if (ret != kIOReturnSuccess) {
        IOLog("VBIOS: CONN parse failed (%d)", ret);
    }

    IOLog("VBIOS: Parsed %u connectors from DCB\n", fConnectorCount);

    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::scanPCIR()
{
    IOLog("VBIOS: Scanning PCIR images\n");

    for (uint32_t off = 0; off < fVBIOSSize && off < 0x20000; off += 512) {
        if (readU16(off) != 0xAA55) continue;

        uint16_t pcirOff = readU16(off + 0x18);
        if (pcirOff < 0x30 || pcirOff > 0x100) continue;

        if (readU32(off + pcirOff) != 0x52494350) continue;

        uint16_t vendor  = readU16(off + pcirOff + 4);
        uint16_t device  = readU16(off + pcirOff + 6);
        uint8_t  codeTyp = readU8(off + pcirOff + 0x14);
        uint16_t imgLen  = readU16(off + pcirOff + 0x10) * 512;
        uint8_t  last    = readU8(off + pcirOff + 0x15) & 0x80;

        const char *typStr = "?";
        if (codeTyp == 0)    typStr = "PCI/AT";
        else if (codeTyp == 3) typStr = "EFI";
        else if (codeTyp == 0xE0) typStr = "FWSEC";

        IOLog("VBIOS: PCIR at 0x%04x ven=0x%04x dev=0x%04x type=%s len=%u%s\n",
              off + pcirOff, vendor, device, typStr, imgLen,
              last ? " (last)" : "");

        if (last) break;
    }

    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::parseDCB()
{
    uint16_t dcbOff = readU16(DCB_ROM_OFFSET);
    if (!dcbOff) {
        IOLog("VBIOS: No DCB table (offset 0x36 = 0)\n");
        return kIOReturnNotFound;
    }

    fDcbVersion = readU8(dcbOff);
    fDcbHdr     = readU8(dcbOff + 1);
    fDcbCount   = readU8(dcbOff + 2);
    fDcbEntryLen = readU8(dcbOff + 3);

    if (fDcbVersion < 0x30) {
        IOLog("VBIOS: DCB version 0x%02x too old (need >=0x30)\n", fDcbVersion);
        return kIOReturnUnsupported;
    }

    if (fDcbVersion >= 0x42) {
        IOLog("VBIOS: DCB version 0x%02x unknown (max supported: 0x41)\n", fDcbVersion);
        return kIOReturnUnsupported;
    }

    if (fDcbEntryLen < 8) {
        IOLog("VBIOS: DCB entry size %u too small\n", fDcbEntryLen);
        return kIOReturnUnsupported;
    }

    IOLog("VBIOS: DCB at 0x%04x ver=0x%02x hdr=%u cnt=%u entry=%u\n",
          dcbOff, fDcbVersion, fDcbHdr, fDcbCount, fDcbEntryLen);

    fConnectorCount = 0;

    for (uint32_t i = 0; i < fDcbCount && i < DCB_MAX_ENTRIES; i++) {
        uint32_t entOff = dcbOff + fDcbHdr + i * fDcbEntryLen;
        uint32_t conn   = readU32(entOff);
        uint32_t conf   = readU32(entOff + 4);
        uint8_t  type   = DCB_TYPE(conn);

        if (type == DCB_OUTPUT_UNUSED || type == DCB_OUTPUT_EOL) continue;

        ConnectorInfo *c = &fConnectors[fConnectorCount];
        c->index   = i;
        c->type    = type;
        c->orId    = DCB_OR(conn);
        c->i2cIndex = DCB_I2C_INDEX(conn);
        c->heads   = DCB_HEADS(conn);
        c->bus     = DCB_BUS(conn);
        c->location = DCB_LOCATION(conn);
        c->link    = DCB_LINK(conf);
        c->extdev  = DCB_EXTDEV(conf);
        c->linkBw  = DCB_DP_BW(conf);
        c->linkNr  = DCB_DP_NR(conf);

        // Map connTag to display connector type
        uint8_t connTag = DCB_CONNECTOR(conn);
        c->connType = CONN_UNUSED;

        if (fDcbVersion >= 0x30) {
            uint16_t connOff = readU16(dcbOff + 0x14);
            if (connOff && connTag < CONN_MAX_ENTRIES) {
                uint32_t connEntryOff = connOff + 5 + connTag * 4;
                if (connEntryOff + 3 < fVBIOSSize) {
                    c->connType = readU8(connEntryOff);
                }
            }
        }

        const char *typeStr = "?";
        if (type == DCB_OUTPUT_DP) typeStr = "DP";
        else if (type == DCB_OUTPUT_TMDS) typeStr = "TMDS/HDMI";
        else if (type == DCB_OUTPUT_ANALOG) typeStr = "ANALOG";
        else if (type == DCB_OUTPUT_LVDS) typeStr = "LVDS";

        IOLog("VBIOS: DCB[%u] type=%s OR=%u i2c=%u bus=%u connType=0x%02x "
              "linkBw=%u linkNr=%u link=%u\n",
              i, typeStr, c->orId, c->i2cIndex, c->bus, c->connType,
              c->linkBw, c->linkNr, c->link);

        fConnectorCount++;
    }

    IOLog("VBIOS: DCB active connectors: %u\n", fConnectorCount);
    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::parseCONN()
{
    uint16_t dcbOff = readU16(DCB_ROM_OFFSET);
    if (!dcbOff || fDcbVersion < 0x30 || fDcbHdr < 0x16) {
        IOLog("VBIOS: CONN table not available (DCB ver 0x%02x, hdr %u)\n",
              fDcbVersion, fDcbHdr);
        return kIOReturnNotFound;
    }

    uint16_t connOff = readU16(dcbOff + 0x14);
    if (!connOff) {
        IOLog("VBIOS: No CONN table pointer\n");
        return kIOReturnNotFound;
    }

    uint8_t cver = readU8(connOff);
    uint8_t chdr = readU8(connOff + 1);
    uint8_t ccnt = readU8(connOff + 2);
    uint8_t clen = readU8(connOff + 3);

    IOLog("VBIOS: CONN at 0x%04x ver=%u hdr=%u cnt=%u len=%u\n",
          connOff, cver, chdr, ccnt, clen);

    if (cver < 0x30 || clen < 4) {
        IOLog("VBIOS: CONN format unsupported\n");
        return kIOReturnUnsupported;
    }

    for (uint32_t i = 0; i < ccnt && i < CONN_MAX_ENTRIES; i++) {
        uint32_t entryOff = connOff + chdr + i * clen;
        uint8_t  ctype = readU8(entryOff);
        uint8_t  bloc  = readU8(entryOff + 1);
        uint8_t  hpdB  = readU8(entryOff + 2);

        uint8_t hpd   = ((hpdB & 0x3) << 2) | ((bloc >> 4) & 0x3);
        uint8_t dpAux = (((hpdB >> 2) & 0x3) << 2) | ((bloc >> 6) & 0x3);
        uint8_t loc   = bloc & 0xF;

        const char *typeStr = "?";
        if (ctype == CONN_DP)      typeStr = "DP";
        else if (ctype == CONN_EDP) typeStr = "eDP";
        else if (ctype == CONN_HDMI) typeStr = "HDMI";
        else if (ctype == CONN_VGA)  typeStr = "VGA";
        else if (ctype == CONN_DVI_I || ctype == CONN_DVI_D) typeStr = "DVI";
        else if (ctype == CONN_USB_C) typeStr = "USB-C";

        IOLog("VBIOS: CONN[%u] type=%s loc=%u hpd=%u dpAux=%u\n",
              i, typeStr, loc, hpd, dpAux);

        // Update matching DCB entries with CONN info
        for (uint32_t j = 0; j < fConnectorCount; j++) {
            if (fConnectors[j].bus == i) {
                fConnectors[j].hpdPin = hpd;
                fConnectors[j].dpAux  = dpAux;
            }
        }
    }

    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::applyMode(DisplayMode *mode)
{
    if (!mode) return kIOReturnBadArgument;

    memcpy(&fCurrentMode, mode, sizeof(DisplayMode));

    for (uint32_t i = 0; i < fConnectorCount; i++) {
        if (fConnectors[i].type == DCB_OUTPUT_DP && i < fDcbCount) {
            configureEncoder(&fConnectors[i], mode);
            setDisplayTimings(mode);
            break;
        }
    }

    IOLog("VBIOS: Mode applied: %ux%u@%u bpp=%u\n",
          mode->width, mode->height, mode->refresh, mode->bpp);

    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::setDisplayTimings(DisplayMode *mode)
{
    mode->hTotal = mode->width + 160;
    mode->vTotal = mode->height + 40;
    mode->hSyncStart = mode->width + 32;
    mode->hSyncEnd = mode->width + 112;
    mode->vSyncStart = mode->height + 8;
    mode->vSyncEnd = mode->height + 28;
    mode->hBlankStart = mode->width;
    mode->hBlankEnd = mode->hTotal;
    mode->vBlankStart = mode->height;
    mode->vBlankEnd = mode->vTotal;
    mode->pitch = mode->width * (mode->bpp / 8);

    return kIOReturnSuccess;
}

IOReturn VBIOSDisplay::configureEncoder(ConnectorInfo *conn, DisplayMode *mode)
{
    if (!conn || !mode) return kIOReturnBadArgument;

    IOLog("VBIOS: Configure OR %u (type=%s) for %ux%u\n",
          conn->orId,
          conn->type == DCB_OUTPUT_DP ? "DP" : "TMDS",
          mode->width, mode->height);

    return kIOReturnSuccess;
}
