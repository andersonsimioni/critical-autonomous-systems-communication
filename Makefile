APP = main

# Compiladores
CXX_X86   = g++
CXX_RISCV = riscv64-linux-gnu-g++

# Flags de compilação e linkagem
CXXFLAGS = -Wall -O2 -std=c++17
LDFLAGS  = --static

# Fontes e objetos
SRC = $(wildcard src/*.cpp)

OBJ_X86   = $(patsubst src/%.cpp, build/x86/%.o, $(SRC))
OBJ_RISCV = $(patsubst src/%.cpp, build/riscv/%.o, $(SRC))

# Arquivos principais
KERNEL_IMAGE = bzImage
INITRAMFS    = initramfs.cpio

# Caminhos
BUSYBOX_INSTALL = busybox/_install
TOOLS_DIR       = tools

# Diretório de logs
LOG_DIR = $(BUSYBOX_INSTALL)/logs

# Alvo padrão: compila x86, gera cpio, roda as VMs e analisa latência
all: clean x86 clean_build cpio run_vms analyze_latency

# Limpeza completa
clean:
	rm -rf build bin logs latency_reports $(INITRAMFS) initramfs.cpio.gz
	@echo "[CLEAN] Tudo limpo."

# Compilação para RISC-V
riscv: $(OBJ_RISCV)
	@echo "[BUILD] Compilando para riscv64..."
	@mkdir -p bin
	$(CXX_RISCV) $(OBJ_RISCV) -o bin/$(APP)-riscv $(LDFLAGS)

# Compilação para x86
x86: $(OBJ_X86)
	@echo "[BUILD] Compilando para x86_64..."
	@mkdir -p bin
	$(CXX_X86) $(OBJ_X86) -o bin/$(APP)-x86 $(LDFLAGS)

# Regras de compilação dos objetos
build/riscv/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CXX_RISCV) $(CXXFLAGS) -c $< -o $@

build/x86/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CXX_X86) $(CXXFLAGS) -c $< -o $@

# Limpa apenas diretório de build temporário
clean_build:
	rm -rf build
	@echo "[CLEAN] Diretório build/ limpo."

# Geração do initramfs
cpio:
	@echo "[CPIO] Gerando initramfs..."
	@cp bin/$(APP)-x86 $(BUSYBOX_INSTALL)/main
	@chmod +x $(BUSYBOX_INSTALL)/init
	@chmod +x $(BUSYBOX_INSTALL)/main
	@mkdir -p $(BUSYBOX_INSTALL)/logs
	@cd $(BUSYBOX_INSTALL) && find . | cpio -o -H newc | gzip > ../../initramfs.cpio.gz
	@gunzip -c initramfs.cpio.gz > $(INITRAMFS)
	@echo "[CPIO] initramfs.cpio gerado com sucesso."

# Execução das VMs com QEMU x86_64
run_vms:
	@echo "[RUN] Iniciando VMs em QEMU x86_64..."
	@bash ./run_vms.sh 10 $(KERNEL_IMAGE) $(INITRAMFS)

# Análise de latência via Python
analyze_latency:
	@echo "[ANALYZE] Calculando latências a partir dos logs..."
	@python3 tools/analyze_latency.py logs

.PHONY: all x86 riscv clean clean_build cpio run_vms analyze_latency
