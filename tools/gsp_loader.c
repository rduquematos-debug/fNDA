// gsp_loader.c — Load GSP firmware to GA104Driver via UserClient
// Compile: gcc -o gsp_loader gsp_loader.c -framework IOKit -framework CoreFoundation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

// GA104UserClient method numbers (must match GA104UserClient.hpp)
#define kMethodLoadFirmware     0
#define kMethodFWAppendChunk    1
#define kMethodFWFinalize       2
#define kMethodBootGSP          3
#define kMethodBootSEC2         4
#define kMethodReadReg32        11
#define kMethodReadCSRs         24

#define FW_CHUNK_SIZE           (1024 * 1024)  // 1MB chunks
#define FW_CHK_SIZE FW_CHUNK_SIZE

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s gsp_firmware.bin booter_load.bin\n", argv[0]);
        return 1;
    }
    
    // Open GSP firmware
    FILE *fw = fopen(argv[1], "rb");
    if (!fw) { perror("firmware"); return 1; }
    fseek(fw, 0, SEEK_END);
    long fwSize = ftell(fw);
    fseek(fw, 0, SEEK_SET);
    uint8_t *fwData = malloc(fwSize);
    fread(fwData, 1, fwSize, fw);
    fclose(fw);
    printf("GSP firmware: %ld bytes\n", fwSize);
    
    // Open bootloader
    FILE *bl = fopen(argv[2], "rb");
    if (!bl) { perror("bootloader"); return 1; }
    fseek(bl, 0, SEEK_END);
    long blSize = ftell(bl);
    fseek(bl, 0, SEEK_SET);
    uint8_t *blData = malloc(blSize);
    fread(blData, 1, blSize, bl);
    fclose(bl);
    printf("Bootloader: %ld bytes\n", blSize);
    
    // Find GA104Driver service
    CFMutableDictionaryRef matching = IOServiceMatching("GA104Device");
    io_iterator_t iter;
    IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter);
    
    io_service_t service = 0;
    io_object_t obj;
    while ((obj = IOIteratorNext(iter))) {
        service = obj;
        break;
    }
    IOObjectRelease(iter);
    
    if (!service) {
        printf("❌ GA104Device not found in IORegistry\n");
        return 1;
    }
    printf("✅ GA104Device found\n");
    
    // Open connection
    io_connect_t connect = 0;
    kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &connect);
    IOObjectRelease(service);
    
    if (kr != KERN_SUCCESS) {
        printf("❌ IOServiceOpen failed: 0x%x\n", kr);
        return 1;
    }
    printf("✅ Connection opened\n");
    
    // Step 1: sLoadFirmware (size, hasBL)
    printf("Step 1: Loading firmware...\n");
    uint64_t scalarIn[2] = { (uint64_t)fwSize, (uint64_t)(blSize > 0 ? 1 : 0) };
    uint64_t scalarOut = 0;
    uint32_t outputCount = 1;
    kr = IOConnectCallScalarMethod(connect, kMethodLoadFirmware, scalarIn, 2, &scalarOut, &outputCount);
    if (kr != KERN_SUCCESS) {
        printf("❌ sLoadFirmware failed: 0x%x\n", kr);
        return 1;
    }
    printf("✅ Firmware slot allocated\n");
    
    // Step 2: sFWAppendChunk (send firmware in 1MB chunks)
    printf("Step 2: Sending firmware chunks...\n");
    for (long offset = 0; offset < fwSize; offset += FW_CHK_SIZE) {
        long chunkSize = (fwSize - offset > FW_CHK_SIZE) ? FW_CHK_SIZE : (fwSize - offset);
        uint8_t *chunk = fwData + offset;
        uint64_t scalarIn[2] = { chunkSize, offset };
        kr = IOConnectCallMethod(connect, kMethodFWAppendChunk, NULL, 0,
                                 chunk, (uint32_t)chunkSize, NULL, NULL, NULL, NULL);
        if (kr != KERN_SUCCESS) {
            printf("❌ sFWAppendChunk at offset %ld failed: 0x%x\n", offset, kr);
            return 1;
        }
        if ((offset / FW_CHK_SIZE) % 10 == 0)
            printf("  %ld/%ld MB\n", offset / (1024*1024), fwSize / (1024*1024));
    }
    printf("✅ Firmware chunks sent (%ld)\n", fwSize / (1024*1024));
    
    // Step 3: sFWFinalize
    printf("Step 3: Finalizing firmware...\n");
    kr = IOConnectCallScalarMethod(connect, kMethodFWFinalize, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("❌ sFWFinalize failed: 0x%x\n", kr);
        return 1;
    }
    printf("✅ Firmware finalized\n");
    
    // Step 4: Boot SEC2
    printf("Step 4: Booting SEC2...\n");
    kr = IOConnectCallScalarMethod(connect, kMethodBootSEC2, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("⚠️ sBootSEC2 returned: 0x%x (may be expected in VFIO)\n", kr);
    } else {
        printf("✅ SEC2 booted\n");
    }
    
    // Step 5: Boot GSP
    printf("Step 5: Booting GSP...\n");
    kr = IOConnectCallScalarMethod(connect, kMethodBootGSP, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        printf("⚠️ sBootGSP returned: 0x%x\n", kr);
    } else {
        printf("✅ GSP booted\n");
    }
    
    // Step 6: Read CSRs
    printf("Step 6: Reading CSRs...\n");
    kr = IOConnectCallScalarMethod(connect, kMethodReadCSRs, NULL, 0, NULL, NULL);
    printf("  CSR read returned: 0x%x\n", kr);
    
    // Step 7: Read some registers
    printf("Step 7: Reading GSP registers...\n");
    for (uint32_t reg = 0x110100; reg <= 0x110108; reg += 4) {
        uint64_t scalarIn = reg;
        uint64_t scalarOut = 0;
        outputCount = 1;
        kr = IOConnectCallScalarMethod(connect, kMethodReadReg32, &scalarIn, 1, &scalarOut, &outputCount);
        if (kr == KERN_SUCCESS)
            printf("  [0x%06x] 0x%08llx\n", reg, scalarOut);
    }
    
    printf("\n✅ Done. Check kernel logs:\n");
    printf("  log show --predicate 'process == \"kernel\"' --last 5m | grep GA104\n");
    
    IOServiceClose(connect);
    free(fwData);
    free(blData);
    return 0;
}
