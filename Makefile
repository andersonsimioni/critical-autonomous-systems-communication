APP = main

# compilers
CXX_X86   = g++
# não vamos usar riscv aqui
# CXX_RISCV = riscv64-linux-gnu-g++

# flags for compile and link
CXXFLAGS = -Wall -O2 -std=c++17
LDFLAGS  = --static

# find all source file
SRC = $(wildcard src/*.cpp)

# objs for x86
OBJ_X86   = $(patsubst src/%.cpp, build/x86/%.o, $(SRC))

all: clean x86 clean_build cpio run_vms

cpio:
	@echo generating cpio...
	@cp bin/main-x86 busybox/main
	# @cp init.sh busybox/init
	@chmod 755 busybox/init
	@chmod 755 busybox/main
	@(cd busybox && find . | cpio -o -H newc --owner 0:0) > initramfs.cpio

run_vms:
	@echo running vms...
	@bash ./run_vms.sh

x86: $(OBJ_X86)
	@echo compiling for x86...
	@mkdir -p bin
	$(CXX_X86) $(OBJ_X86) -o bin/$(APP)-x86 $(LDFLAGS)

build/x86/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo cc $<
	$(CXX_X86) $(CXXFLAGS) -c $< -o $@

clean_build:
	rm -rf build
	@echo clean build	

clean:
	rm -rf build bin initramfs.cpio
	@echo clean all

.PHONY: all x86 clean help
