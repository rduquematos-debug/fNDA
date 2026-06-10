#ifndef VBIOSDisplay_hpp
#define VBIOSDisplay_hpp

#include <libkern/libkern.h>
#include <IOKit/IOService.h>
#include "GA104Regs.h"

#define VBIOS_SCRIPT_MAX        64
#define VBIOS_MODE_TABLE_MAX    128
#define DCB_MAX_ENTRIES         16
#define CONN_MAX_ENTRIES        16

typedef enum {
    DISPLAY_TYPE_NONE    = 0,
    DISPLAY_TYPE_VGA     = 1,
    DISPLAY_TYPE_DVI     = 2,
    DISPLAY_TYPE_HDMI    = 3,
    DISPLAY_TYPE_DP      = 4,
    DISPLAY_TYPE_DP_SST  = 5,
    DISPLAY_TYPE_DP_MST  = 6
} DisplayConnectorType;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t refresh;
    uint32_t pitch;
    uint32_t clockKHz;
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
} DisplayMode;

typedef struct {
    uint32_t index;
    uint32_t type;         // DCB_OUTPUT_* or DISPLAY_TYPE_*
    uint32_t connType;     // CONN_* type from CONN table
    uint32_t orId;         // SOR output resource
    uint32_t i2cIndex;     // I2C DDC channel
    uint32_t heads;        // Head bitmask
    uint32_t bus;          // Physical bus/port
    uint32_t location;     // Physical location
    uint32_t link;         // Link index
    uint32_t extdev;       // External device
    uint32_t linkBw;       // DP link bandwidth
    uint32_t linkNr;       // DP lane count
    uint32_t hpdPin;       // HPD pin
    uint32_t dpAux;        // DP AUX channel
} ConnectorInfo;

class VBIOSDisplay : public OSObject
{
    OSDeclareDefaultStructors(VBIOSDisplay);

public:
    virtual bool init() override;
    virtual void free() override;

    IOReturn parseVBIOS(uint8_t *vbiosData, uint32_t size);
    IOReturn applyMode(DisplayMode *mode);

    uint32_t getConnectorCount() const { return fConnectorCount; }
    ConnectorInfo *getConnector(uint32_t index) { return &fConnectors[index]; }
    DisplayMode *getCurrentMode() { return &fCurrentMode; }

private:
    uint8_t        *fVBIOSData;
    uint32_t        fVBIOSSize;
    DisplayMode     fCurrentMode;
    ConnectorInfo   fConnectors[DCB_MAX_ENTRIES];
    uint32_t        fConnectorCount;
    uint8_t         fDcbVersion;
    uint8_t         fDcbHdr;
    uint8_t         fDcbCount;
    uint8_t         fDcbEntryLen;

    IOReturn scanPCIR(void);
    IOReturn parseDCB(void);
    IOReturn parseCONN(void);
    IOReturn setDisplayTimings(DisplayMode *mode);
    IOReturn configureEncoder(ConnectorInfo *conn, DisplayMode *mode);

    uint8_t  readU8(uint32_t offset);
    uint16_t readU16(uint32_t offset);
    uint32_t readU32(uint32_t offset);
};

#endif /* VBIOSDisplay_hpp */
