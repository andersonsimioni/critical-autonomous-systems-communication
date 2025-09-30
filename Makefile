APP = main

# compilers
CXX_X86   = g++
CXX_RISCV = riscv64-linux-gnu-g++

# flags for compile and link
CXXFLAGS = -Wall -O2 -std=c++17
LDFLAGS  = --static

# find all source file
SRC = $(wildcard src/*.cpp)

# objs for each arch
OBJ_X86   = $(patsubst src/%.cpp, build/x86/%.o, $(SRC))
OBJ_RISCV = $(patsubst src/%.cpp, build/riscv/%.o, $(SRC))

all: clean riscv clean_build cpio run_vms

clean:
	rm -rf build bin
	@echo clean all

riscv: $(OBJ_RISCV)
	@echo compile for riscv64 now...
	@mkdir -p bin
	$(CXX_RISCV) $(OBJ_RISCV) -o bin/$(APP)-riscv $(LDFLAGS)

clean_build:
	rm -rf build
	@echo clean build	

cpio:
	@echo generating cpio...
	@cp bin/$(APP)-riscv busybox/_install/main
	@chmod +x busybox/_install/main
	@cd busybox/_install && find . | cpio -o -H newc | gzip > ../../initramfs.cpio.gz

	@gunzip -c initramfs.cpio.gz > initramfs.cpio

run_vms:
	@echo running vms...
	@bash ./run_vms.sh
	

build/riscv/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo cc $<
	$(CXX_RISCV) $(CXXFLAGS) -c $< -o $@

x86: $(OBJ_X86)
	@echo compiling for x86...
	@mkdir -p bin
	$(CXX_X86) $(OBJ_X86) -o bin/$(APP)-x86 $(LDFLAGS)

build/x86/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo cc $<
	$(CXX_X86) $(CXXFLAGS) -c $< -o $@


.PHONY: all x86 clean help
