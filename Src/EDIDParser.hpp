#ifndef EDIDParser_hpp
#define EDIDParser_hpp

#include <libkern/libkern.h>
#include <string.h>

#define EDID_BLOCK_SIZE     128
#define EDID_MAX_MODES      16
#define EDID_MAX_NAME       32

struct EDIDMode {
    uint16_t width;
    uint16_t height;
    uint32_t pixelClock;     // Hz
    uint16_t hTotal;
    uint16_t vTotal;
    uint16_t hSyncStart;
    uint16_t hSyncEnd;
    uint16_t vSyncStart;
    uint16_t vSyncEnd;
    uint16_t hBlankStart;
    uint16_t hBlankEnd;
    uint16_t vBlankStart;
    uint16_t vBlankEnd;
    uint8_t  refreshHz;
    bool     preferred;
};

class EDIDParser {
public:
    bool parse(const uint8_t *data, uint32_t size) {
        fModeCount = 0;
        fName[0]  = '\0';

        if (!data || size < EDID_BLOCK_SIZE) return false;
        if (data[0] != 0x00) return false;       // EDID header magic
        if (data[1] != 0xFF) return false;

        uint8_t checksum = 0;
        for (uint32_t i = 0; i < EDID_BLOCK_SIZE; i++) checksum += data[i];
        if (checksum != 0) return false;

        fEdid = data;
        fSize = (size < EDID_BLOCK_SIZE) ? size : EDID_BLOCK_SIZE;

        parsePreferredTiming();
        parseDetailedTimings();
        parseEstablishedTimings();
        parseMonitorName();

        if (fModeCount == 0) addFallbackMode();

        return true;
    }

    uint32_t getModeCount() const { return fModeCount; }

    EDIDMode getMode(uint32_t index) const {
        if (index >= fModeCount) {
            EDIDMode zero; bzero(&zero, sizeof(zero));
            return zero;
        }
        return fModes[index];
    }

    const char *getMonitorName() const { return fName; }

private:
    const uint8_t *fEdid  = nullptr;
    uint32_t       fSize  = 0;
    EDIDMode       fModes[EDID_MAX_MODES];
    uint32_t       fModeCount = 0;
    char           fName[EDID_MAX_NAME];

    void addMode(const EDIDMode &m) {
        if (fModeCount >= EDID_MAX_MODES) return;
        if (m.width == 0 || m.height == 0) return;
        for (uint32_t i = 0; i < fModeCount; i++)
            if (fModes[i].width == m.width && fModes[i].height == m.height)
                return;
        fModes[fModeCount++] = m;
    }

    void addFallbackMode() {
        EDIDMode m; bzero(&m, sizeof(m));
        m.width = 1920; m.height = 1080; m.refreshHz = 60; m.preferred = true;
        m.pixelClock = 148500000;
        m.hTotal = 2200; m.vTotal = 1125;
        m.hSyncStart = 2008; m.hSyncEnd = 2052;
        m.vSyncStart = 1084; m.vSyncEnd = 1088;
        m.hBlankStart = 1920; m.hBlankEnd = 2200;
        m.vBlankStart = 1080; m.vBlankEnd = 1125;
        addMode(m);
    }

    void parsePreferredTiming() {
        uint16_t hActive = (fEdid[0x12] | ((uint16_t)(fEdid[0x14] & 0xF0) << 4)) + 1;
        uint16_t vActive = (fEdid[0x13] | ((uint16_t)(fEdid[0x15] & 0xF0) << 4)) + 1;
        uint8_t  aspect  = (fEdid[0x15] >> 6) & 0x03;
        if (hActive < 640 || vActive < 480) return;

        EDIDMode m; bzero(&m, sizeof(m));
        m.width = hActive;
        m.height = vActive;
        m.preferred = true;
        switch (aspect) {
            case 0: break;
            case 1: m.height = m.width * 10 / 16; break;
            case 2: m.height = m.width * 9 / 16; break;
            case 3: m.height = m.width * 3 / 5; break;
        }
        m.refreshHz = fEdid[0x16];
        if (m.refreshHz < 50) m.refreshHz = 60;
        addMode(m);
    }

    void parseDetailedTimings() {
        for (uint32_t off = 0x36; off <= 0x6C; off += 18) {
            if (off + 18 > fSize) break;
            uint32_t pixClkX10k = fEdid[off] | ((uint32_t)fEdid[off + 1] << 8);
            if (pixClkX10k < 1000) {
                // Monitor descriptor, not timing
                continue;
            }

            EDIDMode m; bzero(&m, sizeof(m));
            m.pixelClock = pixClkX10k * 10000;
            m.width  = fEdid[off + 2] | ((uint16_t)(fEdid[off + 4] & 0xF0) << 4);
            m.hTotal = (fEdid[off + 8] | ((uint16_t)(fEdid[off + 11] & 0xF0) << 4)) - m.width;
            m.hBlankStart = m.width;
            m.hBlankEnd   = m.width + m.hTotal;
            m.hSyncStart = fEdid[off + 10] | ((uint16_t)(fEdid[off + 11] & 0xC0) << 2);
            m.hSyncEnd   = fEdid[off + 10] + (fEdid[off + 11] & 0x30);
            if (m.hTotal > 0) m.width = fEdid[off + 2] + ((uint16_t)(fEdid[off + 4] & 0xF0) << 4);
            if (m.hSyncStart < m.hBlankStart) m.hSyncStart += m.hBlankStart;
            if (m.hSyncEnd < m.hSyncStart) m.hSyncEnd += m.hSyncStart;

            m.height = fEdid[off + 5] | ((uint16_t)(fEdid[off + 7] & 0xF0) << 4);
            m.vTotal = (fEdid[off + 13] | ((uint16_t)(fEdid[off + 15] & 0xF0) << 4)) - m.height;
            m.vBlankStart = m.height;
            m.vBlankEnd   = m.height + m.vTotal;
            m.vSyncStart = fEdid[off + 12] >> 4;
            m.vSyncEnd   = m.vSyncStart + (fEdid[off + 12] & 0x0F);
            if (m.vTotal > 0) m.height = fEdid[off + 5] + ((uint16_t)(fEdid[off + 7] & 0xF0) << 4);
            if (m.vSyncStart < m.vBlankStart) m.vSyncStart += m.vBlankStart;
            if (m.vSyncEnd < m.vSyncStart) m.vSyncEnd += m.vSyncStart;

            uint32_t totalPixels = (m.hBlankEnd) * (m.vBlankEnd);
            if (totalPixels > 0)
                m.refreshHz = (uint8_t)(m.pixelClock / totalPixels / 1000);
            if (m.refreshHz < 50 || m.refreshHz > 200) m.refreshHz = 60;

            addMode(m);
        }
    }

    void parseEstablishedTimings() {
        static const struct { uint16_t w, h; uint8_t hz; } est[8] = {
            { 640, 480, 60 },  { 640, 480, 67 },  { 640, 480, 72 },  { 640, 480, 75 },
            { 720, 400, 70 },  { 720, 400, 88 },  { 800, 600, 56 },  { 800, 600, 60 },
        };
        uint8_t et = fEdid[0x23];
        for (int i = 0; i < 8; i++) {
            if (et & (1 << i)) {
                EDIDMode m; bzero(&m, sizeof(m));
                m.width = est[i].w; m.height = est[i].h; m.refreshHz = est[i].hz;
                addMode(m);
            }
        }
        // Standard timings (0x26..0x34)
        for (uint32_t off = 0x26; off <= 0x34; off += 2) {
            uint8_t h = fEdid[off], l = fEdid[off + 1];
            if (h == 0x01 && l == 0x01) break;
            uint16_t hActive = (h + 31) * 8;
            uint8_t aspect = (l >> 6) & 0x03;
            uint16_t vActive = 0;
            switch (aspect) {
                case 0: vActive = hActive * 10 / 16; break;
                case 1: vActive = hActive * 3 / 4; break;
                case 2: vActive = hActive * 4 / 5; break;
                case 3: vActive = hActive * 9 / 16; break;
            }
            if (hActive >= 640 && vActive >= 480) {
                EDIDMode m; bzero(&m, sizeof(m));
                m.width = hActive; m.height = vActive; m.refreshHz = 60;
                addMode(m);
            }
        }
    }

    void parseMonitorName() {
        for (uint32_t off = 0x36; off <= 0x6C; off += 18) {
            if (off + 5 > fSize) break;
            if (fEdid[off + 3] == 0xFC) {
                uint32_t len = fEdid[off] + 1;
                if (len > 13) len = 13;
                uint32_t copyLen = (len < sizeof(fName) - 1) ? len : (sizeof(fName) - 1);
                memcpy(fName, &fEdid[off + 5], copyLen);
                fName[copyLen] = '\0';
                for (char *p = fName; *p; p++)
                    if (*p < 32 || *p > 126) *p = '?';
                return;
            }
        }
        strlcpy(fName, "Unknown Display", sizeof(fName));
    }
};

#endif /* EDIDParser_hpp */
