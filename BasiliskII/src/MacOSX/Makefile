# Reproducible ARM64 JIT opcode generator for the uae_cpu_arm64 target.
# Mirrors the Unix pipeline: table68k -> build68k -> defs68k.c -> gencomp_arm.
PROJECT_DIR ?= $(CURDIR)
CPU_SRC := $(PROJECT_DIR)/../uae_cpu_2026
OUT_DIR := $(PROJECT_DIR)/generated/arm64-jit
GEN_DIR := $(PROJECT_DIR)/compiler
CPPFLAGS := -DUSE_XCODE=1 -DUSE_JIT=1 -DJIT=1 -DCPU_aarch64=1 -DCPU_AARCH64=1 -DAARCH64_ASSEMBLY=1 \
	-I$(PROJECT_DIR) -I$(CPU_SRC) -I$(PROJECT_DIR)/../Unix -I$(GEN_DIR)
CXXFLAGS := -std=gnu++17 -stdlib=libc++ $(CPPFLAGS)

.PHONY: all clean verify
all: $(GEN_DIR)/compemu_arm.cpp $(GEN_DIR)/compstbl_arm.cpp $(GEN_DIR)/comptbl.h verify

$(OUT_DIR):
	mkdir -p "$@"

$(OUT_DIR)/build68k: $(CPU_SRC)/build68k.c | $(OUT_DIR)
	$(CC) $(CPPFLAGS) -o "$@" "$<"

$(OUT_DIR)/defs68k.c: $(OUT_DIR)/build68k $(CPU_SRC)/table68k
	"$<" < "$(CPU_SRC)/table68k" > "$@"
	grep -Eq 'n_defs68k = [1-9][0-9]*;' "$@"

$(OUT_DIR)/gencomp_arm: $(OUT_DIR)/defs68k.c $(CPU_SRC)/readcpu.cpp $(GEN_DIR)/gencomp_arm.c $(PROJECT_DIR)/Makefile
	$(CXX) $(CXXFLAGS) -x c++ "$(OUT_DIR)/defs68k.c" "$(CPU_SRC)/readcpu.cpp" \
		-x c++ "$(GEN_DIR)/gencomp_arm.c" -o "$@"

$(GEN_DIR)/compemu_arm.cpp $(GEN_DIR)/compstbl_arm.cpp $(GEN_DIR)/comptbl.h &: $(OUT_DIR)/gencomp_arm
	cd "$(GEN_DIR)" && "$(OUT_DIR)/gencomp_arm"
	test -s "$(GEN_DIR)/compemu_arm.cpp"
	test -s "$(GEN_DIR)/compstbl_arm.cpp"
	grep -q 'op_smalltbl_0_comp_ff' "$(GEN_DIR)/compstbl_arm.cpp"

verify: $(OUT_DIR)/defs68k.c $(GEN_DIR)/compemu_arm.cpp $(GEN_DIR)/compstbl_arm.cpp $(GEN_DIR)/comptbl.h
	grep -Eq 'n_defs68k = [1-9][0-9]*;' "$(OUT_DIR)/defs68k.c"
	grep -q 'op_smalltbl_0_comp_ff' "$(GEN_DIR)/compstbl_arm.cpp"
	grep -q 'op_smalltbl_0_comp_nf' "$(GEN_DIR)/compstbl_arm.cpp"

clean:
	rm -rf "$(OUT_DIR)"
	rm -f "$(GEN_DIR)/compemu_arm.cpp" "$(GEN_DIR)/compstbl_arm.cpp" "$(GEN_DIR)/comptbl.h"
