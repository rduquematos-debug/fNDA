// gsp_loader.c — Load GSP firmware to GA104Driver via UserClient
// Compile: gcc -o gsp_loader gsp_loader.c -framework IOKit -framework CoreFoundation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

// GA104UserClient method numbers (must match GA104UserClient.hpp)
#define kMethodLoadFirmware     1   // kGA104MethodLoadFirmware
#define kMethodBootGSP          2   // kGA104MethodBootGSP
#define kMethodReadReg32        4   // kGA104MethodReadReg
#define kMethodFWAppendChunk    7   // kGA104MethodFWAppendChunk
#define kMethodFinalize         8   // kGA104MethodFinalize
#define kMethodBootSEC2         14  // kGA104MethodBootSEC2
#define kMethodReadCSRs         18  // kGA104MethodReadCSRs

#define FW_COPY_CHUNK  (1024 * 1024)  // 1MB chunks for memcpy

static kern_return_t callScalar(io_connect_t c, uint32_t sel,
                                uint64_t in, uint64_t *out) {
    uint32_t outCnt = (out != NULL) ? 1 : 0;
    return IOConnectCallScalarMethod(c, sel, &in, (in ? 1 : 0), out, &outCnt);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s gsp_firmware.bin\n", argv[0]);
        printf("  Loads GSP firmware via GA104Driver UserClient\n");
        return 1;
    }

    // Open GSP firmware
    FILE *fw = fopen(argv[1], "rb");
    if (!fw) { perror("firmware"); return 1; }
    fseek(fw, 0, SEEK_END);
    long fwSize = ftell(fw);
    fseek(fw, 0, SEEK_SET);
    uint8_t *fwData = malloc(fwSize);
    if (!fwData) { printf("Out of memory\n"); fclose(fw); return 1; }
    fread(fwData, 1, fwSize, fw);
    fclose(fw);
    printf("GSP firmware: %ld bytes (%.0f MB)\n", fwSize, fwSize / (1024.0 * 1024.0));

    // Find GA104Driver service
    CFMutableDictionaryRef matching = IOServiceMatching("GA104Device");
    io_iterator_t iter;
    IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);

    io_service_t service = 0;
    io_object_t obj;
    while ((obj = IOIteratorNext(iter))) { service = obj; break; }
    IOObjectRelease(iter);

    if (!service) {
        printf("GA104Device not found in IORegistry\n");
        return 1;
    }
    printf("GA104Device found in IORegistry\n");

    // Open connection
    io_connect_t connect = 0;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &connect);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) {
        printf("IOServiceOpen failed: 0x%x (%s)\n", kr, mach_error_string(kr));
        return 1;
    }
    printf("Connection opened\n");

    // Step 1: Allocate firmware buffer
    printf("\n[1/7] Allocating firmware buffer...\n");
    uint64_t sizeIn = (uint64_t)fwSize;
    kr = IOConnectCallScalarMethod(connect, kMethodFWAppendChunk, &sizeIn, 1, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("FWAppendChunk (alloc) failed: 0x%x\n", kr);
        return 1;
    }
    printf("  Buffer allocated: %ld bytes\n", fwSize);

    // Step 2: Map firmware buffer into userspace
    printf("[2/7] Mapping firmware buffer to userspace...\n");
    mach_vm_address_t mapAddr = 0;
    mach_vm_size_t mapSize = 0;
    kr = IOConnectMapMemory64(connect, 2, mach_task_self(),
                              &mapAddr, &mapSize, kIOMapAnywhere);
    if (kr != KERN_SUCCESS) {
        printf("IOConnectMapMemory64 failed: 0x%x\n", kr);
        return 1;
    }
    printf("  Mapped at 0x%llx, size %llu bytes\n", (uint64_t)mapAddr, (uint64_t)mapSize);

    // Step 3: Copy firmware data in 1MB chunks
    printf("[3/7] Copying firmware...\n");
    long totalMB = (fwSize + FW_COPY_CHUNK - 1) / FW_COPY_CHUNK;
    for (long off = 0; off < fwSize; ) {
        long chunk = (fwSize - off > FW_COPY_CHUNK) ? FW_COPY_CHUNK : (fwSize - off);
        memcpy((void*)(uintptr_t)(mapAddr + off), fwData + off, chunk);
        off += chunk;
        if ((off / FW_COPY_CHUNK) % 10 == 0 || off >= fwSize) {
            printf("  \r  Progress: %ld/%ld MB", off / (1024*1024), fwSize / (1024*1024));
            fflush(stdout);
        }
    }
    printf("\n");

    // Step 4: Unmap firmware buffer
    printf("[4/7] Unmapping buffer...\n");
    kr = IOConnectUnmapMemory64(connect, 2, mach_task_self(), mapAddr);
    if (kr != KERN_SUCCESS) {
        printf("  Warning: IOConnectUnmapMemory64 failed: 0x%x\n", kr);
    }

    // Step 5: Finalize firmware loading (loadExternal)
    printf("[5/7] Finalizing firmware...\n");
    uint64_t finalResult = 0;
    uint32_t finalCnt = 1;
    kr = IOConnectCallScalarMethod(connect, kMethodFinalize, NULL, 0, &finalResult, &finalCnt);
    if (kr != KERN_SUCCESS) {
        printf("  Finalize failed: 0x%x (result=%llu)\n", kr, finalResult);
        return 1;
    }
    printf("  Finalize OK (result=%llu)\n", finalResult);

    // Step 6: Boot SEC2
    printf("[6/7] Booting SEC2...\n");
    uint64_t sec2Result = 0;
    uint32_t sec2Cnt = 1;
    kr = IOConnectCallScalarMethod(connect, kMethodBootSEC2, NULL, 0, &sec2Result, &sec2Cnt);
    printf("  SEC2 boot returned: 0x%x (result=%llu)\n", kr, sec2Result);

    // Step 7: Boot GSP (includes bootloader DMA from firmware)
    printf("[7/7] Booting GSP...\n");
    uint64_t gspResult = 0;
    uint32_t gspCnt = 1;
    kr = IOConnectCallScalarMethod(connect, kMethodBootGSP, NULL, 0, &gspResult, &gspCnt);
    printf("  GSP boot returned: 0x%x (result=%llu)\n", kr, gspResult);

    // Post-boot: Read CSRs
    printf("\nReading GSP CSRs...\n");
    uint64_t csrIn = 0;
    uint64_t csrOuts[3] = {0};
    uint32_t csrCnt = 3;
    kr = IOConnectCallScalarMethod(connect, kMethodReadCSRs, NULL, 0, csrOuts, &csrCnt);
    printf("  ReadCSRs ret=0x%x count=%u [0]=0x%llx [1]=0x%llx [2]=0x%llx\n",
           kr, csrCnt, csrOuts[0], csrOuts[1], csrOuts[2]);

    // Post-boot: Read GSP registers via BAR0 offset
    printf("\nGSP registers (BAR0 +0x110000):\n");
    for (uint32_t reg = 0x110100; reg <= 0x110114; reg += 4) {
        uint64_t scalarIn = reg;
        uint64_t scalarOut = 0;
        uint32_t outCnt = 1;
        kr = IOConnectCallScalarMethod(connect, kMethodReadReg32, &scalarIn, 1, &scalarOut, &outCnt);
        printf("  [0x%06x] = 0x%08llx  %s\n", reg, scalarOut,
               kr == KERN_SUCCESS ? "" : "(failed)");
    }

    printf("\nDone. Check kernel logs:\n");
    printf("  log show --predicate 'process == \"kernel\"' --last 5m | grep GA104\n");

    IOServiceClose(connect);
    free(fwData);
    return 0;
}
