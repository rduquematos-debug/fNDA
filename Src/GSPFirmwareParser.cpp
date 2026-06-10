#include "GSPFirmwareParser.hpp"
#include <IOKit/IOLib.h>

#define NVFW_MAGIC  0x000010de

static bool parseHsV2(const uint8_t *data, uint32_t size,
                      const NvFwBinHdr *hdr, GspBootInfo *info)
{
    uint32_t data_off = hdr->data_offset;
    uint32_t data_sz  = hdr->data_size;
    uint32_t desc_off = hdr->header_offset;

    if (desc_off + sizeof(NvFwHsHeaderV2) > size) return false;

    const NvFwHsHeaderV2 *hs = (const NvFwHsHeaderV2 *)(data + desc_off);

    // Validate offsets
    uint32_t sig_prod_off = hs->sig_prod_offset;
    uint32_t meta_off     = hs->meta_data_offset;

    if (sig_prod_off > data_sz || meta_off > data_sz) return false;

    // Read load header from data section at sig_prod_offset
    const uint8_t *dsec = data + data_off;
    if (sig_prod_off + sizeof(NvFwHsLoadHeaderV2) > data_sz) return false;

    const NvFwHsLoadHeaderV2 *ld = (const NvFwHsLoadHeaderV2 *)(dsec + sig_prod_off);

    uint32_t os_code_off  = ld->os_code_offset;
    uint32_t os_code_size = ld->os_code_size;
    uint32_t os_data_off  = ld->os_data_offset;
    uint32_t os_data_size = ld->os_data_size;
    uint32_t apps_off     = ld->apps_offset;
    uint32_t apps_size    = ld->apps_size;

    if (os_code_off + os_code_size > data_sz ||
        os_data_off + os_data_size > data_sz ||
        apps_off + apps_size > data_sz) {
        IOLog("GSPParser HSv2: offsets out of data section bounds\n");
        return false;
    }

    // Reject zero-length segments (wrong format variant)
    if (os_code_size == 0 && apps_size == 0) {
        IOLog("GSPParser HSv2: zero-length segments, trying alternative format\n");
        return false;
    }

    // Metadata: engine_id, ucode_id, fuse_ver
    uint32_t engine_mask = 0;
    uint32_t ucode_id    = 0;
    uint32_t fuse_ver    = 0;
    if (meta_off + sizeof(NvFwMetaData) <= data_sz) {
        const NvFwMetaData *meta = (const NvFwMetaData *)(dsec + meta_off);
        engine_mask = meta->engine_id_mask;
        ucode_id    = meta->ucode_id;
        fuse_ver    = meta->fuse_ver;
    }

    // In HS v2 format:
    // IMEM = apps section (the bootloader code)
    // DMEM = os_data section (bootloader data + manifest)
    uint8_t *imem_ptr = (uint8_t*)(dsec + apps_off);
    uint32_t imem_sz  = apps_size;
    uint8_t *dmem_ptr = (uint8_t*)(dsec + os_data_off);
    uint32_t dmem_sz  = os_data_size;
    uint32_t manifest  = os_data_size; // manifest at end of DMEM

    // Patch location for DMEM signature
    uint32_t patch_loc = hs->patch_loc;
    if (patch_loc > 0 && patch_loc >= os_data_off && patch_loc < os_data_off + os_data_size) {
        uint32_t sig_off = patch_loc - os_data_off;
        IOLog("GSPParser HSv2: patching DMEM signature at offset 0x%x with fuse_ver=0x%x\n",
              sig_off, fuse_ver);
        // Write fuse version signature at the patch location
        *(uint32_t*)(dmem_ptr + sig_off) = fuse_ver;
    }

    info->imem_src        = imem_ptr;
    info->imem_size       = imem_sz;
    info->dmem_src        = dmem_ptr;
    info->dmem_size       = dmem_sz;
    info->boot_addr       = 0;
    info->app_version     = 0;
    info->manifest_offset = manifest;
    info->engine_id_mask  = engine_mask;
    info->ucode_id        = ucode_id;
    info->fuse_ver        = fuse_ver;
    info->valid           = true;

    IOLog("GSPParser HSv2: IMEM=%u DMEM=%u manifest=0x%x engMask=0x%x ucode=%u fuse=0x%x\n",
          imem_sz, dmem_sz, manifest, engine_mask, ucode_id, fuse_ver);
    return true;
}

static bool parseRmUcode(const uint8_t *data, uint32_t size,
                         const NvFwBinHdr *hdr, GspBootInfo *info)
{
    uint32_t desc_off = hdr->header_offset;
    uint32_t data_off = hdr->data_offset;
    uint32_t data_sz  = hdr->data_size;

    if (desc_off + sizeof(RmRiscvUcodeDesc) > size) return false;

    const RmRiscvUcodeDesc *desc = (const RmRiscvUcodeDesc *)(data + desc_off);

    if (desc->field0 != 5) {
        IOLog("GSPParser: unexpected desc field0=%u\n", desc->field0);
        return false;
    }

    uint32_t code_off     = desc->monitorCodeOffset;
    uint32_t data_off_desc = desc->monitorDataOffset;
    uint32_t manifest_off = desc->manifestOffset;

    const uint8_t *dsec = data + data_off;

    if (data_off_desc > data_sz || code_off > data_sz ||
        manifest_off > data_sz) {
        IOLog("GSPParser RmUcode: offsets outside data section\n");
        return false;
    }

    info->imem_src        = (uint8_t*)(dsec + code_off);
    info->imem_size       = manifest_off - code_off;
    info->dmem_src        = (uint8_t*)(dsec + data_off_desc);
    info->dmem_size       = code_off - data_off_desc;
    info->boot_addr       = code_off;
    info->app_version     = desc->appVersion;
    info->manifest_offset = manifest_off;
    info->engine_id_mask  = 0;
    info->ucode_id        = 0;
    info->fuse_ver        = 0;
    info->valid           = true;

    IOLog("GSPParser RmUcode: IMEM at +0x%x (%u bytes), DMEM at +0x%x (%u bytes), "
          "boot=0x%x v=%u\n",
          code_off, info->imem_size,
          data_off_desc, info->dmem_size,
          info->boot_addr, info->app_version);
    return true;
}

bool parseGspBootloader(const uint8_t *data, uint32_t size, GspBootInfo *info) {
    if (!data || !info) return false;
    if (size < sizeof(NvFwBinHdr)) return false;

    const NvFwBinHdr *hdr = (const NvFwBinHdr *)data;

    if (hdr->bin_magic != NVFW_MAGIC) {
        IOLog("GSPParser: bad magic 0x%08x (expected 0x%08x)\n",
              hdr->bin_magic, NVFW_MAGIC);
        return false;
    }

    IOLog("GSPParser: bin_size=%u hdr_off=0x%x data_off=0x%x data_size=%u\n",
          hdr->bin_size, hdr->header_offset, hdr->data_offset, hdr->data_size);

    // Try HS v2 format first (GA104 uses this)
    if (parseHsV2(data, size, hdr, info))
        return true;

    // Fallback to RmRiscvUcodeDesc format
    IOLog("GSPParser: HS v2 failed, trying RmRiscvUcodeDesc...\n");
    return parseRmUcode(data, size, hdr, info);
}

bool parseBooterLoadV3(const uint8_t *data, uint32_t size, GspBootInfo *info)
{
    if (!data || !info) return false;
    if (size < sizeof(NvFwBinHdr) + sizeof(NvFwHsHeaderV2) + 72) return false;

    const NvFwBinHdr *hdr = (const NvFwBinHdr *)data;
    if (hdr->bin_magic != NVFW_MAGIC) {
        IOLog("BooterV3: bad magic 0x%08x\n", hdr->bin_magic);
        return false;
    }

    uint32_t dataOff = hdr->data_offset;
    uint32_t dataSz  = hdr->data_size;
    if (dataOff + dataSz > size) dataSz = size - dataOff;

    const NvFwHsHeaderV2 *hs = (const NvFwHsHeaderV2 *)(data + hdr->header_offset);

    // Descriptor area: right after signature production data
    uint32_t headerEnd = hdr->header_offset + sizeof(NvFwHsHeaderV2) + hs->sig_prod_size;
    uint32_t descOff = headerEnd;
    uint32_t descSz = dataOff - descOff;

    if (descSz < sizeof(NvFwBooterV3Desc)) {
        IOLog("BooterV3: descriptor too small (%u)\n", descSz);
        return false;
    }

    const NvFwBooterV3Desc *d = (const NvFwBooterV3Desc *)(data + descOff);

    if (d->app_count != 1 || d->app_type != 3) {
        IOLog("BooterV3: unexpected app_count=%u type=%u\n", d->app_count, d->app_type);
        return false;
    }

    uint32_t imemOff = d->data_offset;
    uint32_t imemSz  = d->imem_size;
    uint32_t dmemOff = d->dmem_start;
    uint32_t dmemSz  = d->dmem_size;

    // Validate bounds
    if (imemOff + imemSz > dataSz || dmemOff + dmemSz > dataSz) {
        IOLog("BooterV3: sections exceed data section bounds\n");
        return false;
    }
    if (imemSz == 0) {
        IOLog("BooterV3: zero IMEM size\n");
        return false;
    }

    const uint8_t *dsec = data + dataOff;

    info->imem_src        = (uint8_t*)(dsec + imemOff);
    info->imem_size       = imemSz;
    info->dmem_src        = (uint8_t*)(dsec + dmemOff);
    info->dmem_size       = dmemSz;
    info->boot_addr       = 0;
    info->app_version     = d->app_version;
    info->manifest_offset = 0x3C0; // TU102 default PARADDR
    info->engine_id_mask  = 0x00000001; // GSP Falcon
    info->ucode_id        = 0;
    info->fuse_ver        = 0;
    info->is_encrypted    = true;
    info->is_booter_v3    = true;
    info->valid           = true;

    IOLog("BooterV3: IMEM=%u DMEM=%u enc=%d ver=%u\n",
          imemSz, dmemSz, info->is_encrypted, info->app_version);
    return true;
}
