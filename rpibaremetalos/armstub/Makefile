# armstub specific build configuration
ASM := /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-as
LD := /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-ld
OBJCOPY := /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-objcopy

# Source and output files
SRC := src/armstub_minimal.S
OBJ := build/armstub_minimal.o
BIN := image/armstub_minimal.bin
ELF := image/armstub_minimal.elf

# Build rules
all: $(BIN)

$(OBJ): $(SRC)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BIN): $(OBJ)
	$(LD) -nostdlib --section-start=.text=0 -o $(ELF) $^
#	$(OBJCOPY) -O binary $(ELF) $(BIN)
#	rm -f $(ELF)

clean:
	 rm -rf build/
	 mkdir -p build
	 echo "Built armstub to $(BIN)"

default: clean armstub