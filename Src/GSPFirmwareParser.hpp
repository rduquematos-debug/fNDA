#ifndef GSPFirmwareParser_hpp
#define GSPFirmwareParser_hpp

#include <libkern/libkern.h>

// NVFW binary header (24 bytes, always at offset 0)
typedef struct {
    uint32_t bin_magic;       // 0x000010de
    uint32_t bin_ver;         // 1
    uint32_t bin_size;        // total file size
    uint32_t header_offset;   // offset to header (HS v2 or RM_RISCV_UCODE_DESC)
    uint32_t data_offset;     // offset to data section
    uint32_t data_size;      // size of data section
} __attribute__((packed)) NvFwBinHdr;

// HS (Hash Signature) v2 header - used by GA104 GSP bootloader
// Found at bin_hdr->header_offset
typedef struct {
    uint32_t sig_prod_offset;   // signature production data offset in data section
    uint32_t sig_prod_size;     // signature production data size
    uint32_t patch_loc;         // patch location within data section (DMEM signature offset)
    uint32_t patch_sig;         // patch signature identifier
    uint32_t meta_data_offset;  // metadata offset in data section
    uint32_t num_sig;           // number of signatures
} __attribute__((packed)) NvFwHsHeaderV2;

// HS load header v2 - at data section offset specified by sig_prod_offset
typedef struct {
    uint32_t num_apps;
    uint32_t apps_offset;
    uint32_t apps_size;
    uint32_t os_code_offset;
    uint32_t os_code_size;
    uint32_t os_data_offset;
    uint32_t os_data_size;
} __attribute__((packed)) NvFwHsLoadHeaderV2;

// Metadata structure at meta_data_offset
typedef struct {
    uint32_t engine_id_mask;
    uint32_t ucode_id;
    uint32_t fuse_ver;
    uint32_t _pad;
} __attribute__((packed)) NvFwMetaData;

// RM_RISCV_UCODE_DESC (20 bytes) - fallback for older format
typedef struct {
    uint32_t field0;               // = 5
    uint32_t monitorCodeOffset;    // IMEM code offset
    uint32_t monitorDataOffset;    // DMEM data offset
    uint32_t manifestOffset;       // manifest offset
    uint32_t appVersion;           // firmware version
} __attribute__((packed)) RmRiscvUcodeDesc;

// Booter v3 descriptor area (72 bytes at descriptor_offset)
typedef struct {
    uint32_t _pad[3];
    uint32_t total_size;        // 0x0c: total encrypted payload size
    uint32_t _pad2;
    uint32_t app_count;         // 0x14: number of apps (usually 1)
    uint32_t app_version;       // 0x18: version
    uint32_t app_type;          // 0x1c: type (3 = booter)
    uint32_t app_number;        // 0x20: app index
    uint32_t _rsvd0;            // 0x24
    uint32_t data_offset;       // 0x28: IMEM start offset within data section
    uint32_t dmem_start;        // 0x2c: DMEM start offset within data section
    uint32_t dmem_size;         // 0x30: DMEM region size
    uint32_t section_flags;     // 0x34: flags
    uint32_t imem_offset;       // 0x38: IMEM offset (same as data_offset)
    uint32_t imem_size;         // 0x3c: IMEM code size
    uint32_t _rsvd1;            // 0x40
    uint32_t _rsvd2;            // 0x44
} __attribute__((packed)) NvFwBooterV3Desc;

// Parsed bootloader info
typedef struct {
    bool     valid;
    uint8_t *imem_src;       // pointer to IMEM code
    uint32_t imem_size;      // IMEM code size
    uint8_t *dmem_src;       // pointer to DMEM data
    uint32_t dmem_size;      // DMEM data size
    uint32_t boot_addr;      // boot vector (IMEM entry point)
    uint32_t app_version;    // application version
    uint32_t manifest_offset; // manifest offset in DMEM (PARADDR)
    uint32_t engine_id_mask;  // BROM engine ID mask
    uint32_t ucode_id;        // BROM ucode ID
    uint32_t fuse_ver;        // fuse version for DMEM signature patch
    bool     is_encrypted;    // data is BROM-encrypted
    bool     is_booter_v3;    // parsed using booter v3 format
} GspBootInfo;

// Parse NVFW bootloader binary into GspBootInfo
// Tries HS v2 format first, falls back to RmRiscvUcodeDesc
bool parseGspBootloader(const uint8_t *data, uint32_t size, GspBootInfo *info);

// Parse booter_load-*.bin format (Ampere encrypted booter)
// Returns true if the file is a valid booter v3 image
bool parseBooterLoadV3(const uint8_t *data, uint32_t size, GspBootInfo *info);

#endif
