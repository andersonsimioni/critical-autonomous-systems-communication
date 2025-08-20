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

all: clean x86 riscv clean_build cpio run_vms

cpio:
	@echo generating cpio...
	@cp bin/main-riscv busybox/main
	@find ./busybox | cpio -o -H newc > initramfs.cpio 

run_vms:
	@echo running vms...
	@run_vms.sh

x86: $(OBJ_X86)
	@echo compiling for x86...
	@mkdir -p bin
	$(CXX_X86) $(OBJ_X86) -o bin/$(APP)-x86 $(LDFLAGS)

build/x86/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo cc $<
	$(CXX_X86) $(CXXFLAGS) -c $< -o $@

riscv: $(OBJ_RISCV)
	@echo compile for riscv64 now...
	@mkdir -p bin
	$(CXX_RISCV) $(OBJ_RISCV) -o bin/$(APP)-riscv $(LDFLAGS)

build/riscv/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo cc $<
	$(CXX_RISCV) $(CXXFLAGS) -c $< -o $@

clean_build:
	rm -rf build
	@echo clean build	

clean:
	rm -rf build bin
	@echo clean all

.PHONY: all x86 riscv clean help
