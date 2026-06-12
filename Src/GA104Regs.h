#ifndef GA104_REGS_H
#define GA104_REGS_H

#include <stdint.h>

// ============================================================================
// Chip Identification (Ampere / GA104)
// ============================================================================

#define NV_PMC_BOOT_0                     0x00000000
#define NV_PMC_BOOT_0_ARCHITECTURE        24:20
#define NV_PMC_BOOT_0_IMPLEMENTATION      23:20

#define NV_CHIP_ARCH_AMPERE               0x17
#define NV_CHIP_ARCH_ADA                  0x19
#define NV_CHIP_ARCH_BLACKWELL            0x1B

// ============================================================================
// PMC (Power Management Controller)
// ============================================================================

#define NV_PMC_ENABLE                     0x00000200
#define NV_PMC_INTR_EN_0                  0x00000140

// ============================================================================
// PBUS (Bus Control)
// ============================================================================

#define NV_PBUS_PRI_TIMEOUT_SAVE_0        0x00001460
#define NV_PBUS_VBIOS_SCRATCH             0x00001400
#define NV_PBUS_PCI_NV_19                 0x0000184C
#define NV_PBUS_BAR0_ACTIVE               (1 << 0)

// FWSEC status registers
#define NV_PBUS_VBIOS_SCRATCH_FRTS_ERR    (NV_PBUS_VBIOS_SCRATCH + (0x0E * 4))
#define NV_PBUS_VBIOS_SCRATCH_FWSEC_ERR   (NV_PBUS_VBIOS_SCRATCH + (0x15 * 4))

// ============================================================================
// PFB (Framebuffer / Memory Controller)
// ============================================================================

#define NV_PFB_PRI_MMU_CTRL               0x00100C80
#define NV_PFB_PRI_MMU_WPR2_ADDR_LO       0x001FA824
#define NV_PFB_PRI_MMU_WPR2_ADDR_HI       0x001FA828

#define NV_PFB_WPR2_ENABLED(val)          (((val) >> 31) & 1)

// ============================================================================
// FALCON (Secure Co-processor)
// ============================================================================

#define NV_PFALCON_FALCON_BASE            0x00110000

// Falcon v4 (Ampere GA104) IRQ register offsets
// NOTE: Falcon v4 (GA102) offsets differ from v3 (Turing). Verified from NVIDIA dev_falcon_v4.h
#define FALCON_IRQSCLR                    0x0004   // Write 1 to clear/trigger IRQ
#define FALCON_IRQSTAT                    0x0008   // Read pending IRQs
#define FALCON_IRQMSET                    0x0010   // Write 1 to enable IRQ in mask
#define FALCON_IRQMCLR                    0x0014   // Write 1 to disable IRQ in mask
#define FALCON_IRQMASK                    0x0018   // Read current IRQ mask
#define FALCON_IRQDEST                    0x001C   // Each bit: 0=RISC-V, 1=HOST
// NOTE: No FALCON_IRQSSET at 0x0C on Falcon v4 — that was for older Falcon versions!
#define FALCON_MAILBOX0                   0x0040
#define FALCON_MAILBOX1                   0x0044
#define FALCON_ITFEN                      0x0048
#define FALCON_IDLESTATE                  0x004C
#define FALCON_CURCTX                     0x0050
#define FALCON_NXTCTX                     0x0054
#define FALCON_SCRATCH0                   0x0080
#define FALCON_SCRATCH1                   0x0084
#define FALCON_ICD_CS                     0x0180
#define FALCON_ICD_CT                     0x0184
#define FALCON_ICD_RW                     0x0188
#define FALCON_CPUCTL                     0x0100
#define FALCON_BOOTVEC                    0x0104
#define FALCON_OS                         0x0080
#define FALCON_HWCFG                      0x0108
#define FALCON_DMACTL                     0x010C
#define FALCON_DMATRFBASE                 0x0110
#define FALCON_DMATRFMOFFS                0x0114
#define FALCON_DMATRFCMD                  0x0118
#define FALCON_DMATRFFBOFFS               0x011C
#define FALCON_DMATRFBASE1                0x0128
#define FALCON_FBIF_CTL                   0x0624
#define FALCON_FBIF_TRANSCFG(i)           (0x0600 + (i) * 4)
#define FALCON_EXTERRADDR                 0x0168
#define FALCON_EXTERRSTAT                 0x016C
#define FALCON_ENGCTL                     0x01A4
#define FALCON_IMEMC(i)                   (0x0180 + (i) * 16)
#define FALCON_IMEMD(i)                   (0x0184 + (i) * 16)
#define FALCON_IMEMT(i)                   (0x0188 + (i) * 16)
#define FALCON_DMEMC(i)                   (0x01C0 + (i) * 8)
#define FALCON_DMEMD(i)                   (0x01C4 + (i) * 8)
#define FALCON_CPUCTL_ALIAS               0x0130
#define FALCON_RM                         0x0084
#define FALCON_HWCFG2                     0x00F4
#define FALCON_ENGINE                     0x03C0

#define FALCON_BROM_MOD_SEL               0x1180
#define FALCON_BROM_CURR_UCODE_ID         0x1198
#define FALCON_BROM_ENGIDMASK             0x119C
#define FALCON_BROM_PARAADDR              0x1210

#define FALCON_BCR_CTRL                   0x1668
#define FALCON_BCR_CTRL_VALID             (1 << 0)
#define FALCON_BCR_CTRL_CORE_SELECT_RISCV (1 << 4)

#define FALCON_CPUCTL_STARTCPU            (1 << 1)
#define FALCON_CPUCTL_HALTED              (1 << 4)
#define FALCON_CPUCTL_ALIAS_EN            (1 << 6)

#define FALCON_DMACTL_DMEM_SCRUBBING      (1 << 1)
#define FALCON_DMACTL_IMEM_SCRUBBING      (1 << 2)

#define FALCON_HWCFG2_MEM_SCRUBBING       (1 << 12)
#define FALCON_HWCFG2_RESET_READY         (1 << 31)

#define FALCON_IMEMC_AINCW                (1 << 24)
#define FALCON_IMEMC_SECURE               (1 << 28)

#define FALCON_DMEMC_AINCW                (1 << 24)

#define FALCON_IMEM_BLKSIZE2              8
#define FALCON_IMEM_WORDS_PER_BLK         ((1U << (FALCON_IMEM_BLKSIZE2 - 2)) - 1)

#define FALCON_DMATRFCMD_SEC_IMEM         (1 << 2)

#define FALCON_ITFEN_CTXEN                (1 << 0)
#define FALCON_ITFEN_MTHDEN               (1 << 1)
#define FALCON_ITFEN_DTFEN                (1 << 2)

#define FALCON_FBIF_CTL_ALLOW_PHYS_NO_CTX 0x00000080
#define FALCON_FBIF_CTL_ALLOW_PHYS        0x00000100

#define FALCON_TRANSCFG_TARGET_LOCAL_FB   0x00000000
#define FALCON_TRANSCFG_TARGET_COHERENT   0x00000004
#define FALCON_TRANSCFG_TARGET_NON_COHERENT 0x00000005

// ============================================================================
// GSP (GPU System Processor) — Ampere / GA104
// Ampere: RISC-V registers at 0x110000 (within GSP Falcon space)
// ============================================================================

#define NV_PGSP_BASE                      0x00110000

#define NV_PGSP_FALCON_MAILBOX0           (NV_PGSP_BASE + FALCON_MAILBOX0)
#define NV_PGSP_FALCON_MAILBOX1           (NV_PGSP_BASE + FALCON_MAILBOX1)
#define NV_PGSP_FALCON_CPUCTL             (NV_PGSP_BASE + FALCON_CPUCTL)

// Ampere GSP RISC-V registers (offsets from NV_PGSP_BASE)
#define NV_PGSP_RISCV_BOOTVECTOR          0x1000
#define NV_PGSP_RISCV_GO                  0x1004
#define NV_PGSP_RISCV_STATUS              0x1008
#define NV_PGSP_RISCV_ITCMSK              0x100C

// RISC-V IRQDEST: route doorbell IRQ to RISC-V core
#define NV_PRISCV_RISCV_IRQDEST           0x0011152C
#define NV_PRISCV_RISCV_IRQMASK           0x00111528
#define NV_PGSP_QUEUE_HEAD(i)             (0x00110C00 + (i) * 8)
#define NV_PGSP_QUEUE_TAIL(i)             (0x00110C80 + (i) * 8)
#define GSP_DOORBELL                      NV_PGSP_QUEUE_HEAD(GSP_CMDQ_IDX)
// Relative offsets for writeReg32 (fGSPBase = BAR0 + 0x110000, so subtract that base)
#define GSP_QUEUE_HEAD_REL(i)             (0x0C00 + (i) * 8)
#define GSP_QUEUE_TAIL_REL(i)             (0x0C80 + (i) * 8)
#define GSP_DOORBELL_REL                  GSP_QUEUE_HEAD_REL(GSP_CMDQ_IDX)
#define NV_PGSP_RISCV_IRQMASK_REL         (0x1000 + 0x528) // 0x1528 (0x111528 - 0x110000)

// Queue indices
#define GSP_CMDQ_IDX                      0
#define GSP_MSGQ_IDX                      1

#define GSP_STATUS_STOPPED                0
#define GSP_STATUS_BOOTING                1
#define GSP_STATUS_READY                  3

// ============================================================================
// GSP Boot Stages
// ============================================================================

#define BOOT_STAGE_3_HANDOFF              0x3

#define NV_PGC6_BSI_SECURE_SCRATCH_14     0x00118F58

// ============================================================================
// SEC2 (Security Engine 2)
// ============================================================================

#define NV_PSEC_BASE                      0x00840000
#define NV_PSEC_REG(offset)              (NV_PSEC_BASE + (offset))
#define NV_PSEC_FALCON_CPUCTL             (NV_PSEC_BASE + FALCON_CPUCTL)
#define NV_PSEC_FALCON_MAILBOX0           (NV_PSEC_BASE + FALCON_MAILBOX0)
#define NV_PSEC_FALCON_MAILBOX1           (NV_PSEC_BASE + FALCON_MAILBOX1)
#define NV_PSEC_FALCON_BOOTVEC            (NV_PSEC_BASE + FALCON_BOOTVEC)
#define NV_PSEC_FALCON_HWCFG              (NV_PSEC_BASE + FALCON_HWCFG)
#define NV_PSEC_FALCON_DMACTL             (NV_PSEC_BASE + FALCON_DMACTL)
#define NV_PSEC_FALCON_DMATRFBASE         (NV_PSEC_BASE + FALCON_DMATRFBASE)
#define NV_PSEC_FALCON_DMATRFMOFFS        (NV_PSEC_BASE + FALCON_DMATRFMOFFS)
#define NV_PSEC_FALCON_DMATRFCMD          (NV_PSEC_BASE + FALCON_DMATRFCMD)

// SEC2 RISC-V registers (Ampere)
#define NV_PSEC_RISCV_CPUCTL              0x00841388
#define NV_PSEC_RISCV_BR_RETCODE          0x00841400
#define NV_PSEC_RISCV_BCR_CTRL            0x00841668
#define NV_PSEC_RISCV_BCR_DMEM_ADDR       0x0084166C

// SEC2 BROM registers (Falcon offsets @ PSEC base)
#define NV_PSEC_FALCON_BROM_MOD_SEL       (NV_PSEC_BASE + 0x1180)
#define NV_PSEC_FALCON_BROM_CURR_UCODE_ID (NV_PSEC_BASE + 0x1198)
#define NV_PSEC_FALCON_BROM_ENGIDMASK     (NV_PSEC_BASE + 0x119C)
#define NV_PSEC_FALCON_BROM_PARAADDR      (NV_PSEC_BASE + 0x1210)

// ============================================================================
// Timer
// ============================================================================

#define NV_PTIMER_TIME_0                  0x00009400
#define NV_PTIMER_TIME_1                  0x00009410

// ============================================================================
// RPC Message Definitions
// ============================================================================

#define NV_VGPU_MSG_SIGNATURE_VALID       0x43505256

#define NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO    0x48
#define NV_VGPU_MSG_FUNCTION_SET_REGISTRY           0x49
#define NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC           0x67
#define NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL         0x4C
#define NV_VGPU_MSG_FUNCTION_GSP_RM_FREE            0x68

// ============================================================================
// RPC Structures
// ============================================================================

#pragma pack(push, 1)

typedef struct {
    uint32_t signature;
    uint32_t headerVersion;
    uint32_t rpcResult;
    uint32_t rpcResultPriv;
    uint32_t function;
    uint32_t length;
} NvRpcMessageHeader;

typedef struct {
    uint32_t checkSum;
    uint32_t seqNum;
    uint32_t elemCount;
    uint32_t reserved;
    uint8_t  data[];
} GspQueueElement;

// ============================================================================
// GSP Queue Structures
// ============================================================================

typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t           capacity;
    uint32_t           entrySize;
    uint32_t           reserved[4];
} GSPQueueHeader;

typedef struct {
    uint32_t id;
    uint32_t flags;
    uint32_t sequence;
    uint32_t status;
    uint64_t dataPtr;
    uint32_t length;
    uint32_t reserved[5];
    uint8_t  payload[];
} GSPCommand;

// ============================================================================
// GspSystemInfo — Passed to GSP via RPC 0x15
// MUST match NVIDIA's gsp_static_config.h field order exactly
// ============================================================================

#define NV_ACPI_MAX_DISPLAYS              16

struct GspBusInfo {
    uint16_t deviceID;
    uint16_t vendorID;
    uint16_t subdeviceID;
    uint16_t subvendorID;
    uint8_t  revisionID;
};

struct GspDodMethodData {
    uint32_t status;
    uint32_t acpiIdListLen;
    uint32_t acpiIdList[NV_ACPI_MAX_DISPLAYS];
};

struct GspJtMethodData {
    uint32_t status;
    uint32_t jtCaps;
    uint16_t jtRevId;
    uint8_t  bSBIOSCaps;
};

struct GspMuxMethodDataElement {
    uint32_t acpiId;
    uint32_t mode;
    uint32_t status;
};

struct GspMuxMethodData {
    uint32_t tableLen;
    struct GspMuxMethodDataElement acpiIdMuxModeTable[NV_ACPI_MAX_DISPLAYS];
    struct GspMuxMethodDataElement acpiIdMuxPartTable[NV_ACPI_MAX_DISPLAYS];
    struct GspMuxMethodDataElement acpiIdMuxStateTable[NV_ACPI_MAX_DISPLAYS];
};

struct GspCapsMethodData {
    uint32_t status;
    uint32_t optimusCaps;
};

struct GspAcpiMethodData {
    uint8_t                  bValid;
    struct GspDodMethodData  dodMethodData;
    struct GspJtMethodData   jtMethodData;
    struct GspMuxMethodData  muxMethodData;
    struct GspCapsMethodData capsMethodData;
};

struct GspVfInfo {
    uint32_t totalVFs;
    uint32_t firstVFOffset;
    uint64_t FirstVFBar0Address;
    uint64_t FirstVFBar1Address;
    uint64_t FirstVFBar2Address;
    uint8_t  b64bitBar0;
    uint8_t  b64bitBar1;
    uint8_t  b64bitBar2;
};

struct GspPcieConfigReg {
    uint32_t linkCap;
};

typedef struct {
    uint64_t gpuPhysAddr;
    uint64_t gpuPhysFbAddr;
    uint64_t gpuPhysInstAddr;
    uint64_t gpuPhysIoAddr;
    uint64_t nvDomainBusDeviceFunc;
    uint64_t simAccessBufPhysAddr;
    uint64_t notifyOpSharedSurfacePhysAddr;
    uint64_t pcieAtomicsOpMask;
    uint64_t consoleMemSize;
    uint64_t maxUserVa;

    uint32_t pciConfigMirrorBase;
    uint32_t pciConfigMirrorSize;

    uint32_t PCIDeviceID;
    uint32_t PCISubDeviceID;
    uint32_t PCIRevisionID;

    uint32_t pcieAtomicsCplDeviceCapMask;
    uint8_t  oorArch;

    uint64_t clPdbProperties;
    uint32_t Chipset;

    uint8_t  bGpuBehindBridge;
    uint8_t  bFlrSupported;
    uint8_t  b64bBar0Supported;
    uint8_t  bMnocAvailable;
    uint32_t chipsetL1ssEnable;
    uint8_t  bUpstreamL0sUnsupported;
    uint8_t  bUpstreamL1Unsupported;
    uint8_t  bUpstreamL1PorSupported;
    uint8_t  bUpstreamL1PorMobileOnly;
    uint8_t  bSystemHasMux;
    uint8_t  upstreamAddressValid;

    struct GspBusInfo       FHBBusInfo;
    struct GspBusInfo       chipsetIDInfo;
    struct GspAcpiMethodData acpiMethodData;

    uint32_t hypervisorType;
    uint8_t  bIsPassthru;

    uint64_t sysTimerOffsetNs;

    struct GspVfInfo gspVFInfo;

    uint8_t  bIsPrimary;
    uint8_t  isGridBuild;

    struct GspPcieConfigReg pcieConfigReg;
    uint32_t gridBuildCsp;

    uint8_t  bPreserveVideoMemoryAllocations;
    uint8_t  bTdrEventSupported;
    uint8_t  bFeatureStretchVblankCapable;
    uint8_t  bEnableDynamicGranularityPageArrays;
    uint8_t  bClockBoostSupported;

    uint8_t  bRouteDispIntrsToCPU;

    uint64_t hostPageSize;

    // NVIDIA open-gpu-kernel-modules trailing fields (added for struct size match)
    uint8_t  bIsUnixHdmiFrlComplianceEnabled;
    uint8_t  bIsCmcBasedHws;
    uint8_t  bGspNocatEnabled;
    uint8_t  bS0ixSupport;
    uint8_t  bWindowChannelAlwaysMapped;
    uint32_t pciePowerControlValue;
    uint8_t  bPciePowerControlPresent;
    uint32_t pf0DeviceControl2Reg;
    uint8_t  bIsCxlDevice;
} GspSystemInfo;

// ============================================================================
// Libos init arguments
// ============================================================================

#define LIBOS_MEMORY_REGION_LOC_SYSMEM   0
#define LIBOS_MEMORY_REGION_CONTIGUOUS   0

typedef struct {
    uint64_t id8;
    uint64_t pa;
    uint64_t size;
    uint32_t kind;
    uint32_t loc;
} LibosMemoryRegionInitArgument;

#define GSP_FW_WPR_META_MAGIC            0xdc3aae21371a60b3ULL
#define GSP_FW_WPR_META_REVISION         1

// ============================================================================
// WPR Metadata (NVIDIA v535 format)
// ============================================================================

typedef struct {
    uint64_t magic;
    uint32_t version;

    // Bootloader sections (from RM_RISCV_UCODE_DESC)
    uint32_t bootloaderCodeOffset;
    uint32_t bootloaderCodeSize;
    uint32_t bootloaderDataOffset;
    uint32_t bootloaderDataSize;
    uint32_t bootloaderManifestOffset;
    uint32_t _pad0;

    // Radix3 page table for firmware ELF in system memory
    uint64_t sysmemAddrOfRadix3Elf;
    uint64_t sizeOfRadix3Elf;

    // Bootloader binary in system memory
    uint64_t sysmemAddrOfBootloader;
    uint64_t sizeOfBootloader;

    // Cryptographic signature in system memory
    uint64_t sysmemAddrOfSignature;
    uint64_t sizeOfSignature;

    // Heap within WPR
    uint64_t gspFwHeapVirtAddr;
    uint64_t gspFwHeapSize;
    uint64_t gspFwOffset;

    // Boot binary WPR
    uint64_t bootBinVirtAddr;
    uint64_t bootBinSize;

    // FRTS (Firmware Runtime Services) WPR
    uint64_t frtsOffset;
    uint64_t frtsSize;

    // End of WPR (top of region)
    uint64_t gspFwWprEnd;

    // FB size
    uint64_t fbSize;

    // Non-WPR heap
    uint64_t nonWprHeapOffset;
    uint64_t nonWprHeapSize;

    // WPR start
    uint64_t gspFwWprStart;

    uint64_t vgaWorkspaceOffset;
    uint64_t vgaWorkspaceSize;

    uint32_t fwHeapEnabled;
    uint32_t partitionRpcAddr;
    uint32_t partitionRpcRequestOffset;
    uint32_t partitionRpcReplyOffset;
    uint32_t bootCount;
    uint32_t verified;
    uint32_t _pad1[3];
} GspFwWprMeta;

// Size of WPR meta must be 256 bytes
#define GSP_FW_WPR_META_SIZE             256

// ============================================================================
// GSP Message Queue
// ============================================================================

typedef struct {
    uint32_t version;    // 0x00
    uint32_t size;       // 0x04
    uint32_t msgSize;    // 0x08
    uint32_t msgCount;   // 0x0C
    uint32_t writePtr;   // 0x10 ← firmware lê daqui (NVIDIA standard)
    uint32_t flags;      // 0x14
    uint32_t rxHdrOff;   // 0x18
    uint32_t entryOff;   // 0x1C
} GspMsgqTxHeader;

typedef struct {
    uint32_t readPtr;    // 0x00 (NVIDIA msgq_rx = single field)
} GspMsgqRxHeader;

typedef struct {
    uint8_t  authTag[16];
    uint8_t  aad[16];
    uint32_t checksum;
    uint32_t seqNum;
    uint32_t elemCount;
} GspMsgQueueElementPrefix;

#define GSP_RPC_SIGNATURE                0x43505256 // 'RPCV'

// RPC message function numbers (from nvrm/rpcfn.h)
#define NV_VGPU_MSG_EVENT_GSP_INIT_DONE  0x1001
#define NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER 0x1002

// Sequencer opcodes
#define GSP_SEQ_OPCODE_REG_WRITE          0x01
#define GSP_SEQ_OPCODE_REG_READ_MOD_WRITE 0x02
#define GSP_SEQ_OPCODE_REG_MODIFY         0x03
#define GSP_SEQ_OPCODE_REG_POLL           0x04
#define GSP_SEQ_OPCODE_DELAY_US           0x05
#define GSP_SEQ_OPCODE_REG_STORE          0x06
#define GSP_SEQ_OPCODE_CORE_RESET         0x07
#define GSP_SEQ_OPCODE_CORE_START         0x08
#define GSP_SEQ_OPCODE_CORE_WAIT_HALT     0x09
#define GSP_SEQ_OPCODE_CORE_RESUME        0x0A

typedef struct {
    uint32_t mctpHeader;   // [0x00] MCTP: version(4)|SOM(1)|EOM(1)|SEID(8)|DEID(8)|SEQ(8)
    uint32_t nvdmHeader;   // [0x04] NVDM: vendor(16)|reserved(8)|type(8)
    uint32_t checksum;     // [0x08] XOR of all qwords, then HI32^LO32 (must result in 0)
    uint32_t seqNum;       // [0x0C] sequence number
} GspMsgQueuePrefix;

// MCTP header (DMTF DSP0236): SOM=bit31, EOM=bit30, SEQ=29:28, SEID=23:16, DEID=15:8, VER=3:0
#define GSP_MCTP_HEADER(som, eom, ver)  (((som) << 31) | ((eom) << 30) | ((ver) & 0xF))
#define GSP_MCTP_HEADER_SINGLE          GSP_MCTP_HEADER(1, 1, 0x1)   // = 0xC0000001

// NVDM header (MCTP msg header): TYPE=6:0(0x7E), IC=7(0), VENDOR_ID=23:8, NVDM_TYPE=31:24
#define GSP_NVDM_HEADER(nvdmType)       (0x7E | (0x10DE << 8) | ((nvdmType) << 24))
#define GSP_NVDM_HEADER_RM_RPC          GSP_NVDM_HEADER(0x25)        // = 0x2510DE7E

// Full queue element matching NVIDIA GSP_MSG_QUEUE_ELEMENT (payload follows prefix)
typedef struct {
    GspMsgQueuePrefix prefix;
    uint8_t           payload[];
} __attribute__((packed)) GspMsgQueueElement;

// === Checksum: XOR of all 64-bit words, then upper32 ^ lower32 ===
static inline uint32_t gspChecksum32(const void *data, uint32_t len)
{
    const uint64_t *p = (const uint64_t *)data;
    const uint64_t *end = (const uint64_t *)((const uint8_t *)data + len);
    uint64_t sum = 0;
    while (p < end) sum ^= *p++;
    return (uint32_t)((sum >> 32) ^ (sum & 0xFFFFFFFF));
}

// ============================================================================
// nvgMsgq — NVIDIA msgq library replica (from open-gpu-kernel-modules msgq.c)
// ============================================================================

#define NVG_MSGQ_VERSION                  0
#define NVG_MSGQ_MIN_ALIGN                3   // 2^3 = 8
#define NVG_MSGQ_MAX_ALIGN                12  // 4096
#define NVG_MSGQ_FLAGS_SWAP_RX            1
#define NVG_MSGQ_MSG_SIZE_MIN             16

// Internal metadata (NOT in shared memory)
typedef struct {
    // Shared memory header pointers
    GspMsgqTxHeader *pOurTxHdr;     // our TX header (we write)
    volatile const GspMsgqTxHeader *pTheirTxHdr;  // their TX header (they write)
    GspMsgqRxHeader *pOurRxHdr;     // our RX header (we write readPtr)
    volatile const GspMsgqRxHeader *pTheirRxHdr;  // their RX header (they write readPtr)

    // Entry pointers
    uint8_t  *pOurEntries;          // our write buffer start
    const uint8_t *pTheirEntries;   // their write buffer start

    // Read/write pointers (local cache + remote access)
    volatile const uint32_t *pReadIncoming;   // what we read (their readPtr)
    volatile const uint32_t *pWriteIncoming;  // what we read (their writePtr)
    uint32_t *pReadOutgoing;         // what we write (their readPtr)
    uint32_t *pWriteOutgoing;        // what we write (our writePtr)

    // Local copies
    GspMsgqTxHeader tx;             // our TX header (local copy)
    GspMsgqRxHeader rx;             // their RX header (local copy)
    uint32_t txWritePtr;            // local writePtr
    uint32_t txFree;                // cached free space
    uint32_t rxReadPtr;             // local readPtr
    uint32_t rxAvail;               // cached available
    bool     txLinked;
    bool     rxLinked;
    bool     rxSwapped;
} NvMsgqMetadata;

typedef struct {
    uint32_t signature;           // 0x43505256 ('RPCV') — MUST be first!
    uint32_t headerVersion;       // 0x03000000
    uint32_t length;
    uint32_t function;
    uint32_t rpcResult;           // output: 0xFFFFFFFF = pending
    uint32_t rpcResultPrivate;
    uint32_t sequence;
    uint32_t spare;
} GspRpcMessageHeader;

// RPC function numbers (r535 enum from rpc_global_enums.h)
#define NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO  0x48
#define NV_VGPU_MSG_FUNCTION_SET_REGISTRY         0x49
#define NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC         0x67
#define NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL       0x4C
#define NV_VGPU_MSG_FUNCTION_GSP_RM_FREE          0x68
// Events (0x1000+)
#define NV_VGPU_MSG_EVENT_GSP_INIT_DONE           0x1001
#define NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER   0x1002
#define NV_VGPU_MSG_EVENT_POST_EVENT              0x1003

// RM class IDs
#define NV01_ROOT                         0x00000041
#define NV01_DEVICE_0                     0x00000080
#define NV20_SUBDEVICE_0                  0x00002080
#define NV04_DISPLAY_COMMON               0x00730000
#define NV50_DISPLAY_CORE                 0x0000007D
#define NV50_DISPLAY_WNDW                 0x0000007E

// RM control commands (NV2080_CTRL_INTERNAL_DISPLAY)
#define NV2080_CTRL_CMD_INTERNAL_DISPLAY_WRITE_INST_MEM      0x20800A3C
#define NV2080_CTRL_CMD_INTERNAL_DISPLAY_GET_STATIC_INFO     0x20800A48
#define NV0073_CTRL_CMD_SYSTEM_GET_NUM_HEADS                 0x00730003
#define NV0073_CTRL_CMD_SPECIFIC_GET_ALL_HEAD_MASK           0x00730103
#define NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED                 0x00730001
#define NV0073_CTRL_CMD_SPECIFIC_OR_GET_INFO                 0x00730109
#define NV0073_CTRL_CMD_SPECIFIC_GET_CONNECTOR_DATA          0x0073010B
#define NV0073_CTRL_CMD_DP_AUXCH_CTRL                        0x00730111
#define NV0073_CTRL_CMD_DP_CTRL                              0x00730114
#define NV0073_CTRL_CMD_DP_CONFIG_STREAM                     0x00730115
#define NV0073_CTRL_CMD_DFP_GET_ATTACHED_IDS                 0x0073013A
#define NV0073_CTRL_CMD_DFP_GET_INFO                          0x731140
#define NV0073_CTRL_CMD_DFP_ASSIGN_SOR                        0x731152
#define NV0073_CTRL_CMD_DP_SET_LINK_CONFIG                    0x731145
#define NV0073_CTRL_CMD_HEAD_SET_CONTROL                      0x731220
#define NV0073_CTRL_CMD_HEAD_SET_TIMING                       0x731221
#define NV0073_CTRL_CMD_FLIP                                  0x731204

typedef uint32_t NvHandle;

// RM ALLOC params (for NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC)
typedef struct {
    NvHandle  hClient;
    NvHandle  hParent;
    NvHandle  hObject;
    uint32_t  hClass;
    uint32_t  status;       // output
    uint32_t  paramsSize;
    uint32_t  flags;
    uint8_t   params[];     // variable-length
} __attribute__((packed)) GspRmAllocParams;

// RM CONTROL params (for NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL)
typedef struct {
    NvHandle  hClient;
    NvHandle  hObject;
    uint32_t  cmd;
    uint32_t  status;       // output
    uint32_t  paramsSize;
    uint32_t  flags;
    uint8_t   params[];     // variable-length
} __attribute__((packed)) GspRmControlParams;

// Queue sizes
#define GSP_QUEUE_SIZE                   0x40000
#define GSP_QUEUE_MSG_SIZE               0x1000
#define GSP_QUEUE_MSG_COUNT              63
#define GSP_QUEUE_PAGE_SIZE              0x1000

// Mailbox / doorbell
#define GSP_DOORBELL_OFFSET              0x0C00

// ============================================================================
// FRTS (Firmware Runtime Services)
// ============================================================================

#define NVFW_FALCON_APPIF_ID_DMEMMAPPER   0x04
#define NVFW_FALCON_APPIF_DMEMMAPPER_CMD_FRTS 0x15

typedef struct {
    uint32_t signature;    // 0x0
    uint16_t version;      // 0x4
    uint16_t size;         // 0x6
    uint32_t cmdInBufferOffset;    // 0x8
    uint32_t cmdInBufferSize;      // 0xC
    uint32_t cmdOutBufferOffset;   // 0x10
    uint32_t cmdOutBufferSize;     // 0x14
    uint32_t nvfImgDataBufferOffset;
    uint32_t nvfImgDataBufferSize;
    uint32_t printfBufferHdr;
    uint32_t ucodeBuildTimeStamp;
    uint32_t ucodeSignature;
    uint32_t initCmd;           // 0x34 — set to FRTS cmd
    uint32_t ucodeFeature;
    uint32_t ucodeCmdMask0;
    uint32_t ucodeCmdMask1;
    uint32_t multiTgtTbl;
} DmemMapperAppifV3;

typedef struct {
    uint32_t ver;         // 1
    uint32_t hdr;         // sizeof(readVbios)
    uint64_t addr;        // 0 = use VBIOS in FB
    uint32_t size;        // 0
    uint32_t flags;       // 2 = skip VBIOS read
} FrtsReadVbiosCmd;

typedef struct {
    uint32_t ver;         // 1
    uint32_t hdr;         // sizeof(frtsRegion)
    uint32_t addr;        // FRTS base address >> 12 (4K pages)
    uint32_t size;        // FRTS region size >> 12
    uint32_t type;        // NVFW_FRTS_CMD_REGION_TYPE_FB = 2
} FrtsRegionCmd;

#define NVFW_FRTS_CMD_REGION_TYPE_FB      2

// ============================================================================
// VBIOS / FWSEC Structures
// ============================================================================

// --- DCB (Device Control Block) ---
// Format: nouveau kernel dcb.c, versão 0x40/0x41 (Ampere GA104)
// DCB pointer at VBIOS offset 0x36 (u16 LE)

#define DCB_ROM_OFFSET                    0x36
#define DCB_SIGNATURE                     0x4EDCBDCB

// DCB output types (from nouveau dcb.h)
#define DCB_OUTPUT_ANALOG                 0x0
#define DCB_OUTPUT_TV                     0x1
#define DCB_OUTPUT_TMDS                   0x2
#define DCB_OUTPUT_LVDS                   0x3
#define DCB_OUTPUT_DP                     0x6
#define DCB_OUTPUT_WFD                    0x8
#define DCB_OUTPUT_EOL                    0xE
#define DCB_OUTPUT_UNUSED                 0xF

// DCB entry (8 bytes, version >= 0x20)
typedef struct {
    uint32_t conn;  // [0-3] connector info
    // conn bitfields:
    //   [3:0]   type       DCB_OUTPUT_*
    //   [7:4]   i2c_index  I2C bus index
    //   [11:8]  heads      Head bitmask
    //   [15:12] connector  Connector tag (CONN table index)
    //   [19:16] bus        Physical bus/port
    //   [21:20] location   0=int, 1=back, 2=front
    //   [27:22] or         Output Resource (SOR) number
    uint32_t conf;  // [4-7] configuration
    // conf bitfields (version >= 0x40):
    //   [3:0]   link       Link index
    //   [7:4]   link_aux   Link auxiliary
    //   [15:8]  extdev     External device address
    //   [20:16] dither     Dithering mode
    //   [23:21] dp_bw      DP link bandwidth (0=1.62G, 1=2.7G, 2=5.4G, 3=8.1G)
    //   [27:24] dp_nr      DP lane count (encoded: 1/2/3/4/0xF=4)
    //   [29:28] dp_unk
} __attribute__((packed)) DcbEntry;

// Extract fields from DcbEntry
#define DCB_TYPE(conn)                    ((conn) & 0xF)
#define DCB_I2C_INDEX(conn)               (((conn) >> 4) & 0xF)
#define DCB_HEADS(conn)                   (((conn) >> 8) & 0xF)
#define DCB_CONNECTOR(conn)               (((conn) >> 12) & 0xF)
#define DCB_BUS(conn)                     (((conn) >> 16) & 0xF)
#define DCB_LOCATION(conn)                (((conn) >> 20) & 0x3)
#define DCB_OR(conn)                      (((conn) >> 22) & 0x3F)
#define DCB_LINK(conf)                    ((conf) & 0x3)
#define DCB_EXTDEV(conf)                  (((conf) >> 8) & 0xFF)
#define DCB_DP_BW(conf)                   (((conf) >> 21) & 0x7)
#define DCB_DP_NR(conf)                   (((conf) >> 24) & 0xF)

// DP link bandwidth values
#define DCB_DP_BW_162G                    0
#define DCB_DP_BW_270G                    1
#define DCB_DP_BW_540G                    2
#define DCB_DP_BW_810G                    3

// DP lane count mapping
#define DCB_DP_NR_1                       1
#define DCB_DP_NR_2                       2
#define DCB_DP_NR_3                       3
#define DCB_DP_NR_4                       4
#define DCB_DP_NR_4_ALT                   0xF

// --- CONN (Connector Descriptor) Table ---
// Pointer at DCB offset 0x14 (when header >= 0x16)
// Entry: 4 bytes (versions 0x30/0x40)

typedef struct {
    uint8_t  type;        // CONN type (DP, HDMI, VGA, etc)
    uint8_t  location;    // [3:0] location, [5:4] hpd_lo, [7:6] dp_lo
    uint8_t  hpd_mid     : 2;  // bits [1:0]
    uint8_t  dp_mid      : 2;  // bits [3:2]
    uint8_t  di          : 4;  // bits [7:4]
    uint8_t  hpd_hi      : 3;  // bits [2:0]
    uint8_t  sr          : 1;  // bit [3]
    uint8_t  lcdid       : 3;  // bits [6:4]
} __attribute__((packed)) ConnEntry;

#define CONN_TYPE(ce)                     ((ce).type)
#define CONN_HPD(ce)                      (((ce).hpd_mid << 2) | ((ce).location >> 4 & 0x3))
#define CONN_DP_AUX(ce)                   (((ce).dp_mid << 2) | (((ce).location >> 6) & 0x3))
#define CONN_DI(ce)                       ((ce).di)
#define CONN_SR(ce)                       ((ce).sr)
#define CONN_LCDID(ce)                    ((ce).lcdid)

// Connector type values (from nouveau conn.c / bios.h)
#define CONN_VGA                          0x00
#define CONN_COMPOSITE                    0x10
#define CONN_S_VIDEO                      0x11
#define CONN_DVI_I                        0x30
#define CONN_DVI_D                        0x31
#define CONN_LVDS                         0x40
#define CONN_LVDS_SPWG                    0x41
#define CONN_DP                           0x46
#define CONN_EDP                          0x47
#define CONN_HDMI                         0x61
#define CONN_HDMI_C                       0x63
#define CONN_WFD                          0x70
#define CONN_USB_C                        0x71
#define CONN_UNUSED                       0xFF

#define VBIOS_ROM_SIGNATURE               0xAA55
#define VBIOS_ROM_OFFSET                  0x300000
#define VBIOS_MAX_SIZE                    0x100000

#define VBIOS_IMAGE_TYPE_PCIAT            0x00
#define VBIOS_IMAGE_TYPE_EFI              0x03
#define VBIOS_IMAGE_TYPE_FWSEC            0xE0

#define PCI_ROM_SIGNATURE                 0x55AA
#define PCIR_SIGNATURE                    0x52494350

#define PCIR_VENDOR_OFFSET                0x04
#define PCIR_DEVICE_OFFSET                0x06
#define PCIR_IMAGE_LEN_OFFSET             0x10
#define PCIR_CODE_TYPE_OFFSET             0x14
#define PCIR_INDICATOR_OFFSET             0x15
#define PCIR_LAST_IMAGE_FLAG              0x80

#define BIT_HEADER_ID                     0xB8FF
#define BIT_HEADER_SIGNATURE              0x00544942
#define BIT_TOKEN_FALCON_DATA             0x70
#define BIT_TOKEN_PMU_TABLE               0x50
#define BIT_TOKEN_NOP                     0x00

#define FWSEC_APP_ID_FWSEC                0x85

typedef struct {
    uint16_t signature;
    uint8_t  reserved[0x16];
    uint16_t pciDataOffset;
} VbiosRomHeader;

typedef struct {
    uint32_t signature;
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t vpdOffset;
    uint16_t length;
    uint8_t  revision;
    uint8_t  classCode[3];
    uint16_t imageLength;
    uint16_t codeRevision;
    uint8_t  codeType;
    uint8_t  indicator;
    uint16_t maxRuntimeSize;
} VbiosPcirHeader;

typedef struct {
    uint16_t id;
    uint32_t signature;
    uint16_t version;
    uint8_t  headerSize;
    uint8_t  tokenSize;
    uint8_t  tokenCount;
    uint8_t  flags;
} __attribute__((packed)) BitHeader;

typedef struct {
    uint8_t  id;
    uint8_t  dataVersion;
    uint16_t dataSize;
    uint16_t dataOffset;
} __attribute__((packed)) BitToken;

// Falcon ucode descriptor (v3 NVIDIA format)
typedef struct {
    uint32_t vDesc;
    uint32_t storedSize;
    uint32_t pkcDataOffset;
    uint32_t interfaceOffset;
    uint32_t imemPhysBase;
    uint32_t imemLoadSize;
    uint32_t imemVirtBase;
    uint32_t dmemPhysBase;
    uint32_t dmemLoadSize;
    uint16_t engineIdMask;
    uint8_t  ucodeId;
    uint8_t  signatureCount;
    uint16_t signatureVersions;
    uint16_t reserved;
} FalconUcodeDescV3Nvidia;

// ============================================================================
// RM Object Classes
// ============================================================================

#define NV01_ROOT                         0x00000000
#define NV01_ROOT_CLIENT                  0x00000041
#define NV01_DEVICE_0                     0x00000080
#define NV01_MEMORY_LOCAL_USER            0x000000C1
#define NV04_MAP_MEMORY                   0x0000003E
#define NV20_SUBDEVICE_0                  0x00002080
#define FERMI_VASPACE_A                   0x000090F1
#define NVA06C_CTRL_GPFIFO_SCHEDULE      0xA06C0101

// ============================================================================
// Compute classes
// ============================================================================

#define AMPERE_COMPUTE_A                  0x0000C5C0
#define AMPERE_CHANNEL_GPFIFO_A           0x0000C56F

// ============================================================================
// Helper Macros
// ============================================================================

#define NV_DRF_VAL(drf, val)  (((val) >> (drf & 0xFF)) & ((1 << ((drf >> 8) - (drf & 0xFF) + 1)) - 1))
#define NV_MASK(hi, lo)       (((1 << ((hi) - (lo) + 1)) - 1) << (lo))
#define NV_MEMORY_BARRIER()   __asm__ __volatile__ ("mfence" ::: "memory")

// ============================================================================
// GSP Firmware ELF Sections
// ============================================================================

#define GSP_FW_SECTION_IMAGE              ".fwimage"
#define GSP_FW_SECTION_SIG_GA10X          ".fwsignature_ga10x"

// ============================================================================
// Legacy Display Engine Registers (GA104/Ampere, direct without GSP)
// Based on Nouveau tu102_disp_init + gv100 display
// ============================================================================

// --- Display Engine Init ---
#define NV_PDISP_OWNERSHIP                0x006254E8
#define NV_PDISP_PIN_CAPS                 0x00640008
#define NV_PDISP_ENABLE                   0x00610078
#define NV_PDISP_INST_MEM_TARGET          0x00610010
#define NV_PDISP_INST_MEM_ADDR            0x00610014
#define NV_PDISP_HEAD_MASK                0x00610060
#define NV_PDISP_SOR_MASK                 0x00610060  // bits [15:8]

// --- Shadow Register Control ---
#define NV_PDISP_SOR_CAP_SHADOW_CTL       0x00640000
#define NV_PDISP_HEAD_CAP_SHADOW_CTL      0x00640040
#define NV_PDISP_WNDW_CAP_SHADOW_CTL      0x00640004
#define NV_PDISP_IHUB_CAP_SHADOW_CTL      0x0064000C
#define NV_PDISP_SOR_CAP_SHADOW(i)        (0x00640144 + (i) * 8)
#define NV_PDISP_HEAD_RG_SHADOW(i)        (0x00640048 + (i) * 0x20)
#define NV_PDISP_HEAD_POSTCOMP_SHADOW(i)  (0x00640680 + (i) * 0x20)
#define NV_PDISP_WNDW_CAP_SHADOW(i)       (0x00640780 + (i) * 0x20)
#define NV_PDISP_IHUB_CAP_SHADOW(i)       (0x00640010 + (i) * 4)

// --- Hardware Capability Registers (read source for shadow) ---
#define NV_PDISP_SOR_CAP_HW(i)            (0x0061C000 + (i) * 0x800)
#define NV_PDISP_HEAD_RG_HW(i)            (0x00616300 + (i) * 0x800)
#define NV_PDISP_HEAD_POSTCOMP_HW(i)      (0x00616140 + (i) * 0x800)
#define NV_PDISP_WNDW_CAP_HW(i)           (0x00630100 + (i) * 0x800)
#define NV_PDISP_IHUB_CAP_HW(i)           (0x0062E000 + (i) * 4)

// --- Interrupt Masks ---
#define NV_PDISP_INTR_CTRL_MSK            0x00611CF0
#define NV_PDISP_INTR_CTRL_EN             0x00611DB0
#define NV_PDISP_INTR_HEAD_MSK(i)         (0x00611CC0 + (i) * 4)
#define NV_PDISP_INTR_HEAD_EN(i)          (0x00611D80 + (i) * 4)
#define NV_PDISP_INTR_OR_MSK              0x00611CF4
#define NV_PDISP_INTR_OR_EN               0x00611DB4
#define NV_PDISP_INTR_EXC_OTHER_MSK       0x00611CEC
#define NV_PDISP_INTR_EXC_OTHER_EN        0x00611DAC
#define NV_PDISP_INTR_EXC_WINIM_MSK       0x00611CE8
#define NV_PDISP_INTR_EXC_WINIM_EN        0x00611DA8
#define NV_PDISP_INTR_EXC_WIN_MSK         0x00611CE4
#define NV_PDISP_INTR_EXC_WIN_EN          0x00611DA4

// --- Head Timing Registers (GA10x, GV100+) ---
// Each head has 0x400 bytes of register space
#define NV_PHEAD_SET_CONTROL(i)           (0x00682004 + (i) * 0x400)
#define NV_PHEAD_SET_PIXEL_CLOCK(i)       (0x0068200C + (i) * 0x400)
#define NV_PHEAD_SET_PIXEL_CLOCK_FREQ(i)  (0x00682020 + (i) * 0x400)
#define NV_PHEAD_SET_HEAD_TIMING(i)       (0x00682064 + (i) * 0x400)
#define NV_PHEAD_SET_HEAD_VSYNC(i)        (0x00682068 + (i) * 0x400)
#define NV_PHEAD_SET_HEAD_BLANK(i)        (0x0068206C + (i) * 0x400)
#define NV_PHEAD_SET_HEAD_BLACK(i)        (0x00682070 + (i) * 0x400)
#define NV_PHEAD_SET_DITHER_CTL(i)        (0x00682080 + (i) * 0x400)
#define NV_PHEAD_SET_BASE(i)              (0x00682100 + (i) * 0x400)
#define NV_PHEAD_SET_BASE_LIGHT(i)        (0x00682110 + (i) * 0x400)
#define NV_PHEAD_VLINE(i)                 (0x00616330 + (i) * 0x800)
#define NV_PHEAD_HLINE(i)                 (0x00616334 + (i) * 0x800)
#define NV_PHEAD_SET_CONTROL_DEPTH_24BPP  0x00000040   // mode 4 << 4

// --- Window / Framebuffer Registers ---
#define NV_PWINDOW_SET_BASE(i)            (0x00630000 + (i) * 0x800)
#define NV_PWINDOW_SET_SIZE(i)            (0x00630010 + (i) * 0x800)
#define NV_PWINDOW_SET_PITCH(i)           (0x00630014 + (i) * 0x800)
#define NV_PWINDOW_SET_FORMAT(i)          (0x00630018 + (i) * 0x800)
#define NV_PWINDOW_SET_SURFACE(i)         (0x00630100 + (i) * 0x800)
#define NV_PWINDOW_FORMAT_B8G8R8A8        0x000000CF
#define NV_PWINDOW_FORMAT_R8G8B8A8        0x000000CE
#define NV_PWINDOW_FORMAT_X8R8G8B8        0x00000085

// --- SOR (Serial Output Resource) Registers ---
// Pre-Ampere SOR clock (GF119+): 0x00612300 + i*0x800
// Ampere SOR clock (GA10x):      0x00ec04   + i*0x10
#define NV_PSOR_CLK(i)                    (0x00612300 + (i) * 0x800)  // pre-Ampere
#define NV_PSOR_CLK_AMPERE(i)             (0x00ec04 + (i) * 0x10)     // Ampere (Nouveau ga102.c)
#define NV_PSOR_CLK_AMPERE_TMDS           0x00000000   // TMDS = 0 on Ampere
#define NV_PSOR_POWER_STATE(i)            (0x0061C004 + (i) * 0x800)
#define NV_PSOR_DP_CTL(i, l)             (0x0061C10C + (i) * 0x800 + ((l)==2 ? 0x80 : 0))
#define NV_PSOR_DP_PATTERN(i)             (0x0061C110 + (i) * 0x800)
#define NV_PSOR_DP_DRIVE_DC(i, l)        (0x0061C118 + (i) * 0x800 + ((l)==2 ? 0x80 : 0))
#define NV_PSOR_DP_DRIVE_PE(i, l)        (0x0061C120 + (i) * 0x800 + ((l)==2 ? 0x80 : 0))
#define NV_PSOR_BL_PWM(i)                (0x0061C084 + (i) * 0x800)
#define NV_PSOR_AUX_ADDR(i)              (0x0061C0F0 + (i) * 0x800)
#define NV_PSOR_AUX_DATA(i)              (0x0061C0F4 + (i) * 0x800)
#define NV_PSOR_AUX_CTL(i)               (0x0061C0F8 + (i) * 0x800)
#define NV_PSOR_AUX_STAT(i)              (0x0061C0FC + (i) * 0x800)

// SOR clock values
#define NV_PSOR_CLK_TMDS_165             0x00280000   // TMDS mode, speed=0x0a
#define NV_PSOR_CLK_TMDS_340             0x00500000   // TMDS mode, speed=0x14

// SOR power
#define NV_PSOR_POWER_ON                 0x80000001   // bit 16+0 = power on, poll bit 31
#define NV_PSOR_POWER_OFF                0x00000000
#define NV_PSOR_POWER_BUSY               0x80000000   // bit 31

// --- HDMI Registers (per head) ---
#define NV_PSOR_HDMI_CTRL(i)             (0x006165C0 + (i) * 0x800)
#define NV_PSOR_HDMI_CTRL_ENABLE         0x40000000
#define NV_PSOR_HDMI_AVI_CTRL(i)         (0x006F0000 + (i) * 0x400)
#define NV_PSOR_HDMI_AVI_HEADER(i)       (0x006F0008 + (i) * 0x400)
#define NV_PSOR_HDMI_AVI_SUB0_L(i)       (0x006F000C + (i) * 0x400)
#define NV_PSOR_HDMI_AVI_SUB0_H(i)       (0x006F0010 + (i) * 0x400)
#define NV_PSOR_HDMI_AVI_SUB1_L(i)       (0x006F0014 + (i) * 0x400)
#define NV_PSOR_HDMI_AVI_SUB1_H(i)       (0x006F0018 + (i) * 0x400)
#define NV_PSOR_HDMI_VSI_CTRL(i)         (0x006F0100 + (i) * 0x400)
#define NV_PSOR_HDMI_GCP_CTRL(i)         (0x006F00C0 + (i) * 0x400)
#define NV_PSOR_HDMI_ACR(i)              (0x006F0080 + (i) * 0x400)
#define NV_PSOR_HDA_DEVICE_ENTRY(i)      (0x00616528 + (i) * 0x800)
#define NV_PSOR_HDA_CODEC(i)             (0x00616548 + (i) * 0x800)

// --- VPLL (Pixel PLL) Registers ---
#define NV_PVPLL_CONFIG(i)               (0x0000EF00 + (i) * 0x40)
#define NV_PVPLL_CONFIG_DEFAULT          0x02080004
#define NV_PVPLL_N_FN(i)                 (0x0000EF18 + (i) * 0x40)
#define NV_PVPLL_P_M(i)                  (0x0000EF04 + (i) * 0x40)
#define NV_PVPLL_ENABLE(i)               (0x0000E9C0 + (i) * 0x04)
#define NV_PVPLL_ENABLE_ON               0x00000001

// --- Display Core/Window Channel Pushbuffer (GV100+/Ampere) ---
// Core channel
#define NV_PDISP_CORE_PB_ADDR_LO           0x00610B24
#define NV_PDISP_CORE_PB_ADDR_HI           0x00610B20
#define NV_PDISP_CORE_PB_CTL1              0x00610B28
#define NV_PDISP_CORE_PB_CTL2              0x00610B2C
#define NV_PDISP_CORE_CHANNEL_CTL          0x006104E0
#define NV_PDISP_CORE_PUT                  0x00680000
#define NV_PDISP_CORE_GET                  0x00680004
#define NV_PDISP_CORE_STATUS               0x00610630
#define NV_PDISP_CORE_IDLE                 0x000B0000  // bits [19:16] = 0xB when idle

// Window channel 0
#define NV_PDISP_WINDOW_PB_ADDR_LO         0x00610B34
#define NV_PDISP_WINDOW_PB_ADDR_HI         0x00610B30
#define NV_PDISP_WINDOW_PB_CTL1            0x00610B38
#define NV_PDISP_WINDOW_PB_CTL2            0x00610B3C
#define NV_PDISP_WINDOW_CHANNEL_CTL        0x006104E4
#define NV_PDISP_WINDOW_PUT                0x00690000
#define NV_PDISP_WINDOW_GET                0x00690004

// VRAM flush register
#define NV_PFIFO_FLUSH                     0x00070000

// Supervisor trigger registers (alternative to pushbuffer)
#define NV_PDISP_SUPERVISOR_CTL            0x006107A8
#define NV_PDISP_SUPERVISOR_DATA           0x00611860

// Pushbuffer method format macros
#define PUSH_METHOD_HDR(method, count) \
    (0 << 29) | (((count) - 1) << 14) | (((uint32_t)(method) >> 2) & 0x3FFF)
#define PUSH_JUMP_HDR(offset_dwords) \
    (1 << 29) | ((offset_dwords) & 0x1FFFFFFF)
#define PUSH_DATA(value)  (value)
#define END_PB_SEGMENT    (7 << 29)

// Notifier types for SET_NOTIFIER_CONTROL
#define NV_NOTIFIER_WRITE_ONLY         0x1
#define NV_NOTIFIER_WRITE_PAGETABLE    0x3

// Ampere display class IDs (from Nouveau ga102.c / open-gpu-kernel-modules)
#define GA104_DISP                        0x0000c670   // Display common class
#define GA104_DISP_CORE_CHANNEL_DMA       0x0000c67d   // Core channel (NVC67D)
#define GA104_DISP_WINDOW_CHANNEL_DMA     0x0000c67e   // Window channel (NVC67E)

// Window channel method offsets
// NVC37E (Turing, 0xc37e) and NVC67E (Ampere, 0xc67e) share the same method offsets
// From open-gpu-kernel-modules clc37e.h / clc67e.h
#define NVC37E_UPDATE                      0x00000200
#define NVC67E_UPDATE                      0x00000200   // same offset, Ampere class
#define NVC37E_SET_SIZE                    0x00000224
#define NVC37E_SET_STORAGE                 0x00000228
#define NVC37E_SET_PARAMS                  0x0000022C
#define NVC37E_SET_CONTEXT_DMA_ISO(b)      (0x00000240 + (b) * 4)
#define NVC37E_SET_OFFSET(b)               (0x00000260 + (b) * 4)
#define NVC37E_SET_POINT_IN(b)             (0x00000290 + (b) * 4)
#define NVC37E_SET_SIZE_IN                 0x00000298
#define NVC37E_SET_SIZE_OUT                0x000002A4
#define NVC37E_SET_PRESENT_CONTROL         0x00000308
#define NVC37E_SET_INTERLOCK_FLAGS         0x00000300
#define NVC37E_SET_WINDOW_INTERLOCK_FLAGS  0x00000304
// NVC67E aliases (Ampere, same offsets as NVC37E)
#define NVC67E_UPDATE                      NVC37E_UPDATE
#define NVC67E_SET_SIZE                    NVC37E_SET_SIZE
#define NVC67E_SET_STORAGE                 NVC37E_SET_STORAGE
#define NVC67E_SET_PARAMS                  NVC37E_SET_PARAMS
#define NVC67E_SET_CONTEXT_DMA_ISO(b)      NVC37E_SET_CONTEXT_DMA_ISO(b)
#define NVC67E_SET_OFFSET(b)               NVC37E_SET_OFFSET(b)
#define NVC67E_SET_POINT_IN(b)             NVC37E_SET_POINT_IN(b)
#define NVC67E_SET_SIZE_IN                 NVC37E_SET_SIZE_IN
#define NVC67E_SET_SIZE_OUT                NVC37E_SET_SIZE_OUT
#define NVC67E_SET_PRESENT_CONTROL         NVC37E_SET_PRESENT_CONTROL
#define NVC67E_SET_INTERLOCK_FLAGS         NVC37E_SET_INTERLOCK_FLAGS
#define NVC67E_SET_WINDOW_INTERLOCK_FLAGS  NVC37E_SET_WINDOW_INTERLOCK_FLAGS
#define NVC67E_SET_PARAMS_FORMAT_R8G8B8A8  NVC37E_SET_PARAMS_FORMAT_R8G8B8A8
#define NVC67E_SET_PARAMS_FORMAT_B8G8R8A8  NVC37E_SET_PARAMS_FORMAT_B8G8R8A8
#define NVC67E_SET_PARAMS_FORMAT_A8R8G8B8  NVC37E_SET_PARAMS_FORMAT_A8R8G8B8
#define NVC67E_SET_PARAMS_FORMAT_X8R8G8B8  NVC37E_SET_PARAMS_FORMAT_X8R8G8B8
#define NVC67E_SET_PARAMS_KIND_LINEAR      NVC37E_SET_PARAMS_KIND_LINEAR

// Core channel method offsets
// NVC37D (Turing, 0xc37d) and NVC67D (Ampere, 0xc67d) share the same method offsets
// From open-gpu-kernel-modules clc37d.h
#define NVC37D_UPDATE                      0x00000200
#define NVC37D_SET_CONTEXT_DMA_NOTIFIER    0x00000208
#define NVC37D_SET_NOTIFIER_CONTROL        0x0000020C
#define NVC37D_SET_CONTROL                 0x00000210
#define NVC37D_SET_INTERLOCK_FLAGS         0x00000218
#define NVC37D_SET_WINDOW_INTERLOCK_FLAGS  0x0000021C
// NVC67D aliases (Ampere, same offsets as NVC37D)
#define NVC67D_UPDATE                      NVC37D_UPDATE
#define NVC67D_SET_CONTEXT_DMA_NOTIFIER    NVC37D_SET_CONTEXT_DMA_NOTIFIER
#define NVC67D_SET_NOTIFIER_CONTROL        NVC37D_SET_NOTIFIER_CONTROL
#define NVC67D_SET_CONTROL                 NVC37D_SET_CONTROL
#define NVC67D_SET_INTERLOCK_FLAGS         NVC37D_SET_INTERLOCK_FLAGS
#define NVC67D_SET_WINDOW_INTERLOCK_FLAGS  NVC37D_SET_WINDOW_INTERLOCK_FLAGS

// Pushbuffer sizes and addresses
#define DISP_PB_CORE_SIZE                  0x1000  // 4KB for core
#define DISP_PB_WINDOW_SIZE                0x1000  // 4KB for window
#define DISP_PB_TOTAL_SIZE                 0x2000  // 8KB total

// Framebuffer format for NVC37E
#define NVC37E_SET_PARAMS_FORMAT_R8G8B8A8  0x000000CE
#define NVC37E_SET_PARAMS_FORMAT_B8G8R8A8  0x000000CF
#define NVC37E_SET_PARAMS_FORMAT_A8R8G8B8  0x000000C6
#define NVC37E_SET_PARAMS_FORMAT_X8R8G8B8  0x000000E6
#define NVC37E_SET_PARAMS_KIND_LINEAR      0x00000000  // bit 8 = 0 for linear

// --- Display timing for 1920x1080 @ 60Hz ---
#define TIMING_1920x1080_60_PCLOCK_KHZ   148500
#define TIMING_1920x1080_60_HTOTAL        2200
#define TIMING_1920x1080_60_VTOTAL        1125
#define TIMING_1920x1080_60_HSYNC_START   1952   // 1920 + 32
#define TIMING_1920x1080_60_HSYNC_END     2032   // 1920 + 112
#define TIMING_1920x1080_60_VSYNC_START   1088   // 1080 + 8
#define TIMING_1920x1080_60_VSYNC_END     1108   // 1080 + 28
#define TIMING_1920x1080_60_HBLANK_START  2200   // htotal
#define TIMING_1920x1080_60_HBLANK_END    1920
#define TIMING_1920x1080_60_VBLANK_START  1125   // vtotal
#define TIMING_1920x1080_60_VBLANK_END    1080

#pragma pack(pop)

#endif /* GA104_REGS_H */
