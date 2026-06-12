KEXT_NAME = GA104Driver
KEXT_ID = com.nvidia.driver.GA104

SRC = GA104Device.cpp GA104GSPBoot.cpp GA104GSPRPC.cpp GA104Display.cpp GA104FBProvider.cpp GA104Framebuffer.cpp GA104UserClient.cpp GSPFirmware.cpp GSPFirmwareParser.cpp GSPQueue.cpp GSPProtocol.cpp VBIOSDisplay.cpp KmodInfo.cpp fnda_nvkms_interface.cpp nvkms-rmapi.c
OBJ = $(SRC:.cpp=.o)
OBJ := $(OBJ:.c=.o)
HDR = GA104Device.hpp GA104FBProvider.hpp GA104Framebuffer.hpp GA104UserClient.hpp GSPFirmware.hpp GSPQueue.hpp GSPProtocol.hpp VBIOSDisplay.hpp

SDKROOT = $(shell xcrun --show-sdk-path)
SDK_FLAGS = -isysroot $(SDKROOT) -mmacosx-version-min=13.0

CXX = xcrun -sdk macosx clang++
CXXFLAGS = -std=c++11 -c -Wall \
    -DKERNEL -DKERNEL_PRIVATE \
    -I. \
    -I$(SDKROOT)/System/Library/Frameworks/Kernel.framework/Headers \
    $(SDK_FLAGS) \
    -arch x86_64 \
    -fno-rtti -fno-exceptions -fno-builtin \
    -mkernel -nostdlib

LDFLAGS = -Xlinker -kext -arch x86_64 \
    -nostdlib \
    -lkmod -lcc_kext \
    $(SDK_FLAGS)

KEXT_DIR = ../$(KEXT_NAME).kext
KEXT_BIN = $(KEXT_DIR)/Contents/MacOS/$(KEXT_NAME)
KEXT_INFO = $(KEXT_DIR)/Contents/Info.plist
KEXT_RESOURCES = $(KEXT_DIR)/Contents/Resources

.PHONY: all clean install load unload test compile-test load-test logs-test test-all

all: $(KEXT_BIN) $(KEXT_INFO) resources

$(KEXT_BIN): $(OBJ)
	mkdir -p $(KEXT_DIR)/Contents/MacOS
	$(CXX) $(LDFLAGS) -o $@ $^

$(KEXT_INFO): ../Info.plist
	mkdir -p $(KEXT_DIR)/Contents
	cp ../Info.plist $(KEXT_INFO)

resources:
	mkdir -p $(KEXT_RESOURCES)
	@echo "Resources ready (firmware loaded via userspace)"

%.o: %.cpp $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $<

nvkms-rmapi.o: nvkms-rmapi.c $(HDR)
	$(CXX) $(CXXFLAGS) -x c++ -o $@ $<

install: $(KEXT_BIN) $(KEXT_INFO) resources
	cp -r $(KEXT_DIR) /tmp/$(KEXT_NAME).kext
	sudo chown -R root:wheel /tmp/$(KEXT_NAME).kext
	sudo chmod -R 755 /tmp/$(KEXT_NAME).kext

load: install
	sudo kmutil load -v --bundle-path /tmp/$(KEXT_NAME).kext

unload:
	sudo kmutil unload -v --bundle-path /tmp/$(KEXT_NAME).kext

clean:
	rm -f $(OBJ)
	rm -rf $(KEXT_DIR)/Contents/MacOS
	rm -rf $(KEXT_RESOURCES)

# Test targets
compile-test:
	@echo "=== GA104 Compile Test ==="
	@$(MAKE) clean --no-print-directory
	@$(MAKE) all --no-print-directory
	@if [ -f $(KEXT_BIN) ]; then \
		echo "PASS: $(KEXT_BIN) built successfully"; \
		file $(KEXT_BIN); \
	else \
		echo "FAIL: Binary not found"; \
		exit 1; \
	fi

load-test:
	@echo "=== GA104 Load Test ==="
	@$(MAKE) install --no-print-directory
	sudo kmutil load -v --bundle-path /tmp/$(KEXT_NAME).kext || \
		echo "INFO: Load may fail without GPU. Check logs with 'make logs-test'"

logs-test:
	@echo "=== GA104 Logs ==="
	log show --predicate 'process == "kernel"' --last 5m 2>/dev/null | grep -i GA104 || \
		echo "No GA104 logs in last 5 minutes"

test-all:
	bash ../tests/test_all.sh
