.SUFFIXES:
.DELETE_ON_ERROR:

PROJECT := MyOS
VERSION := 0.4.0
ARCH := x86_64

BUILD_DIR := build
KERNEL := $(BUILD_DIR)/myos.elf
ISO := $(BUILD_DIR)/myos-$(ARCH).iso
ISO_ROOT := $(BUILD_DIR)/iso_root
LIMINE_TARBALL := $(BUILD_DIR)/limine-binary.tar.gz
LIMINE_DIR := $(BUILD_DIR)/limine-binary
SMOKE_LOG := $(BUILD_DIR)/smoke-serial.log
LIMINE_CONFIG ?= limine.conf
HOST_CXX ?= c++
HOST_TEST_SOURCES := tests/unit.cpp kernel/src/input_decoder.cpp kernel/src/graphics.cpp kernel/src/memory.cpp kernel/src/smbios.cpp

TOOLCHAIN ?= llvm
TOOLCHAIN_PREFIX ?=
QEMUFLAGS ?= -m 256M
QEMU_DISPLAY ?= gtk

ifeq ($(TOOLCHAIN),llvm)
    ifeq ($(origin CXX),default)
        CXX := clang++
    endif
    TARGET_FLAGS := --target=x86_64-unknown-none-elf
    LINKER_FLAGS := -fuse-ld=lld
else
    ifeq ($(origin CXX),default)
        CXX := $(TOOLCHAIN_PREFIX)g++
    endif
    TARGET_FLAGS :=
    LINKER_FLAGS :=
endif

CPPFLAGS := -Ikernel/include -MMD -MP
WARNFLAGS := -Wall -Wextra
COMMON_FLAGS := \
    $(TARGET_FLAGS) \
    -ffreestanding \
    -fno-common \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-pic \
    -fno-pie \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mno-mmx \
    -mno-sse \
    -mno-red-zone \
    -mcmodel=kernel

CXXFLAGS ?= -g -O2 -pipe
KERNEL_CXXFLAGS := \
    $(CXXFLAGS) \
    $(WARNFLAGS) \
    $(COMMON_FLAGS) \
    -std=gnu++20 \
    -fno-exceptions \
    -fno-rtti \
    -fno-use-cxa-atexit \
    -nostdinc++

KERNEL_ASFLAGS := $(COMMON_FLAGS)
KERNEL_LDFLAGS := \
    $(TARGET_FLAGS) \
    $(LINKER_FLAGS) \
    -nostdlib \
    -static \
    -Wl,-m,elf_x86_64 \
    -Wl,-z,max-page-size=0x1000 \
    -Wl,-z,noexecstack \
    -Wl,--gc-sections \
    -Wl,-T,kernel/linker/x86_64.ld

SOURCES := $(shell find kernel/src \( -name '*.cpp' -o -name '*.S' \) 2>/dev/null | sort)
OBJECTS := $(addsuffix .o,$(addprefix $(BUILD_DIR)/obj/,$(patsubst kernel/src/%,%,$(SOURCES))))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all
all: iso

.PHONY: kernel
kernel: $(KERNEL)

.PHONY: iso
iso: $(ISO)

.PHONY: run
run: $(ISO)
	@echo "Click inside QEMU; use View > Grab Input if needed. Ctrl+Alt+G toggles capture; F1 opens Terminal."
	qemu-system-x86_64 -M q35 $(QEMUFLAGS) -display $(QEMU_DISPLAY) -cdrom $(ISO) -boot d -serial file:$(BUILD_DIR)/run-serial.log

.PHONY: run-mouse
run-mouse: $(ISO)
	@echo "SDL over X11: click inside the window, then Ctrl+Alt+G to toggle capture."
	SDL_VIDEODRIVER=x11 qemu-system-x86_64 -M q35 $(QEMUFLAGS) -display sdl -cdrom $(ISO) -boot d -serial file:$(BUILD_DIR)/run-serial.log

.PHONY: debug
debug: $(ISO)
	qemu-system-x86_64 -M q35 $(QEMUFLAGS) -display $(QEMU_DISPLAY) -cdrom $(ISO) -boot d -S -s

.PHONY: smoke
smoke: $(ISO)
	sh scripts/smoke-test.sh $(ISO) $(SMOKE_LOG)

.PHONY: test-host
test-host:
	@mkdir -p $(BUILD_DIR)
	$(HOST_CXX) -std=c++20 -Wall -Wextra -Werror -Ikernel/include $(HOST_TEST_SOURCES) -o $(BUILD_DIR)/unit-tests
	$(BUILD_DIR)/unit-tests

.PHONY: test-host-sanitized
test-host-sanitized:
	@mkdir -p $(BUILD_DIR)
	$(HOST_CXX) -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -Ikernel/include $(HOST_TEST_SOURCES) -o $(BUILD_DIR)/unit-tests-sanitized
	$(BUILD_DIR)/unit-tests-sanitized

.PHONY: desktop-test
desktop-test: $(ISO)
	python3 scripts/desktop-test.py $(ISO)

.PHONY: test
test: test-host test-host-sanitized smoke desktop-test memory-test cpu-test panic-test timer-test

.PHONY: memory-test
memory-test: $(ISO)
	python3 scripts/memory-test.py

.PHONY: cpu-test
cpu-test: $(ISO)
	python3 scripts/cpu-test.py $(ISO)

.PHONY: panic-test
panic-test: $(ISO)
	python3 scripts/panic-test.py

.PHONY: timer-test
timer-test: $(ISO)
	python3 scripts/timer-test.py

.PHONY: check-tools
check-tools:
	sh scripts/check-tools.sh

$(KERNEL): $(OBJECTS) kernel/linker/x86_64.ld Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(KERNEL_LDFLAGS) $(OBJECTS) -o $@

$(BUILD_DIR)/obj/%.cpp.o: kernel/src/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(KERNEL_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.S.o: kernel/src/%.S Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(KERNEL_ASFLAGS) -c $< -o $@

$(LIMINE_TARBALL):
	@mkdir -p $(BUILD_DIR)
	curl -fL -o $@ https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz

$(LIMINE_DIR)/limine: $(LIMINE_TARBALL)
	@rm -rf $(LIMINE_DIR)
	gunzip < $(LIMINE_TARBALL) | tar -xf - -C $(BUILD_DIR)
	$(MAKE) -C $(LIMINE_DIR)

$(ISO): $(KERNEL) $(LIMINE_DIR)/limine $(LIMINE_CONFIG)
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL) $(ISO_ROOT)/boot/myos.elf
	cp $(LIMINE_CONFIG) $(ISO_ROOT)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)
	$(LIMINE_DIR)/limine bios-install $(ISO)
	@rm -rf $(ISO_ROOT)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/obj $(KERNEL) $(ISO) $(ISO_ROOT) $(SMOKE_LOG)

.PHONY: distclean
distclean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

