# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../Makefile.x64.mk

SRC_ROOT := src
CPP_TEST_SRC_ROOT := test/src
TEST_BUILD_ROOT := test/build
OBJ_DIR := test/build
TEST_OBJ_DIR := test/build

CPP_TEST_SRC_DIRS := test/src test/src/filesystem test/src/filesystem/fat32_filesystem test/src/utility

C_SRC   :=  
CPP_SRC :=  src/c/platform/platform_sw_rngs.cpp \
			src/c/services/os_entity_registry.cpp \
			src/c/services/murmur_hash.cpp \
			src/c/services/xoroshiro128plusplus.cpp \
			src/c/services/uuid.cpp \
			src/c/filesystem/filesystem_errors.cpp \
			src/c/filesystem/master_boot_record.cpp \
			src/c/filesystem/filesystem_path.cpp \
			src/c/filesystem/file_map.cpp \
			src/c/filesystem/fat32_blockio_adapter.cpp \
			src/c/filesystem/fat32_filenames.cpp \
			src/c/filesystem/fat32_directory_cluster.cpp \
			src/c/filesystem/fat32_directory.cpp \
			src/c/filesystem/fat32_file.cpp \
			src/c/filesystem/fat32_filesystem.cpp

CPP_TEST_SRC := $(foreach sdir,$(CPP_TEST_SRC_DIRS),$(wildcard $(sdir)/*.cpp))

TEST_BUILD_DIRS := $(patsubst $(SRC_ROOT)/c/%,$(TEST_BUILD_ROOT)/c/%,$(sort $(patsubst %/,%,$(dir $(CPP_SRC))) $(patsubst %/,%,$(dir $(C_SRC))) ))
TEST_BUILD_DIRS += $(patsubst $(CPP_TEST_SRC_ROOT)%,$(TEST_BUILD_ROOT)%, $(CPP_TEST_SRC_DIRS))

OBJ := $(patsubst $(SRC_ROOT)/c/%.c,$(TEST_BUILD_ROOT)/c/%.o,$(C_SRC)) $(patsubst $(SRC_ROOT)/c/%.cpp,$(TEST_BUILD_ROOT)/c/%.o,$(CPP_SRC))
TEST_OBJ := $(patsubst $(CPP_TEST_SRC_ROOT)/%.cpp,$(TEST_BUILD_ROOT)/%.o,$(CPP_TEST_SRC))

TEST_EXE := $(TEST_OBJ_DIR)/cpputest_main.exe

INCLUDE_DIRS := -Iinclude -I../minimalstdio/include -I../minimalclib/include -I../minimalstdlib/include $(INCLUDE_DIRS) -I$(CPPUTEST_PATH)/include 
LDFLAGS += -L../minimalclib/lib/x64 -L../minimalstdio/lib/x64 -L../minimalstdlib/lib/x64 -L$(CPPUTEST_PATH)/lib
LDLIBS = -lminimalclib -lminimalstdio -lminimalstdlib -lCppUTest -lCppUTestExt

CDEFINES += -D__NO_LOGGING__

#
#	Test target follows
#
#	This looks a bit complex but the foreach loop and the make-... macros serve to create targets for each c or cpp source
# 		file in the C_SRC or CPP_SRC list.
#

test: clean checkdirs $(TEST_EXE)

$(TEST_EXE) : $(TEST_OBJ) $(OBJ)
	$(LD) $(OBJ) $(TEST_OBJ) $(LDFLAGS) $(LDLIBS) $(TEST_LIB) -o $(TEST_EXE)
	-./$(TEST_EXE)


define make-c-goal
$1: $2
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) $(TEST_C_FLAGS) $(TEST_OPTIMIZATION_FLAGS) -c $$< -o $$@
endef

define make-cpp-goal
$1: $2
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(TEST_CPP_FLAGS) $(TEST_OPTIMIZATION_FLAGS) $(CDEFINES) -c $$< -o $$@
endef

$(foreach file,$(C_SRC), $(eval $(call make-c-goal,$(patsubst $(SRC_ROOT)/c/%.c,$(TEST_BUILD_ROOT)/c/%.o,$(file)),$(file))))
$(foreach file,$(CPP_SRC), $(eval $(call make-cpp-goal,$(patsubst $(SRC_ROOT)/c/%.cpp,$(TEST_BUILD_ROOT)/c/%.o,$(file)),$(file))))

$(foreach file,$(CPP_TEST_SRC), $(eval $(call make-cpp-goal,$(patsubst $(CPP_TEST_SRC_ROOT)/%.cpp,$(TEST_BUILD_ROOT)/%.o,$(file)),$(file))))


#
#	Test coverage target follows
#
#	As with the test target, this section of the makefile generates targets for each C or CPP source file with the
#		coverage complier switches included.
#

COVERAGE_BUILD_ROOT := test/coverage
COVERAGE_OBJ_DIR := test/coverage

COVERAGE_BUILD_DIRS := $(patsubst $(SRC_ROOT)/c/%,$(COVERAGE_BUILD_ROOT)/c/%,$(sort $(patsubst %/,%,$(dir $(CPP_SRC))) $(patsubst %/,%,$(dir $(C_SRC))) ))
COVERAGE_BUILD_DIRS += $(patsubst $(CPP_TEST_SRC_ROOT)%,$(COVERAGE_BUILD_ROOT)%, $(CPP_TEST_SRC_DIRS))

COVERAGE_OBJ := $(patsubst $(SRC_ROOT)/c/%.c,$(COVERAGE_BUILD_ROOT)/c/%.o,$(C_SRC)) $(patsubst $(SRC_ROOT)/c/%.cpp,$(COVERAGE_BUILD_ROOT)/c/%.o,$(CPP_SRC))
COVERAGE_TEST_OBJ := $(patsubst $(CPP_TEST_SRC_ROOT)/%.cpp,$(COVERAGE_BUILD_ROOT)/%.o,$(CPP_TEST_SRC))

COVERAGE_EXE := $(COVERAGE_OBJ_DIR)/coverage.exe


test-coverage : clean checkdirs $(COVERAGE_EXE)
	cd $(COVERAGE_OBJ_DIR)
	gcov ../cpputest_main.cpp --object-directory .
	lcov --capture --directory . --output-file $(COVERAGE_OBJ_DIR)/test_coverage.info
	lcov --remove $(COVERAGE_OBJ_DIR)/test_coverage.info '/usr/include/*' '$(CPPUTEST_PATH)/*' --output-file $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info
	genhtml $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info --output-directory $(COVERAGE_OBJ_DIR)/coverage_report

$(COVERAGE_EXE) : $(COVERAGE_TEST_OBJ) $(COVERAGE_OBJ) 
	$(LD) $(COVERAGE_OBJ) $(COVERAGE_TEST_OBJ) $(LDFLAGS) $(LDLIBS) $(TEST_LIB) -lgcov -o $(COVERAGE_EXE)
	./$(COVERAGE_EXE)


define make-coverage-c-goal
$1: $2
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) $(COVERAGE_OPTIMIZATION_FLAGS) $(COVERAGE_C_FLAGS) -c $$< -o $$@
endef

define make-coverage-cpp-goal
$1: $2
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(TEST_CPP_FLAGS) $(COVERAGE_OPTIMIZATION_FLAGS) $(CDEFINES) $(COVERAGE_CPP_FLAGS) -c $$< -o $$@
endef

$(foreach file,$(C_SRC), $(eval $(call make-coverage-c-goal,$(patsubst $(SRC_ROOT)/c/%.c,$(COVERAGE_BUILD_ROOT)/c/%.o,$(file)),$(file))))
$(foreach file,$(CPP_SRC), $(eval $(call make-coverage-cpp-goal,$(patsubst $(SRC_ROOT)/c/%.cpp,$(COVERAGE_BUILD_ROOT)/c/%.o,$(file)),$(file))))

$(foreach file,$(CPP_TEST_SRC), $(eval $(call make-coverage-cpp-goal,$(patsubst $(CPP_TEST_SRC_ROOT)/%.cpp,$(COVERAGE_BUILD_ROOT)/%.o,$(file)),$(file))))


#
#	Other directpry maintenance and diagnostic targets
# 

checkdirs: $(TEST_BUILD_DIRS) $(COVERAGE_BUILD_DIRS)

$(TEST_BUILD_DIRS):
	@mkdir -p $@

$(COVERAGE_BUILD_DIRS):
	@mkdir -p $@

clean:
	@rm -rf $(TEST_BUILD_ROOT)
	@rm -rf $(COVERAGE_BUILD_ROOT)

echo:
	@echo "Build Directories:      			" $(TEST_BUILD_DIRS)
	@echo "C Files:                			" $(C_SRC)
	@echo "CPP Files:              			" $(CPP_SRC)
	@echo "Object Files:           			" $(OBJ)
	@echo "CPP Test Source Directories:     " $(CPP_TEST_SRC_DIRS)
	@echo "CPP Test Files:         			" $(CPP_TEST_SRC)
	@echo "CPP Test Object Files:  			" $(TEST_OBJ)
	@echo "Coverage Build Directories:		" $(COVERAGE_BUILD_DIRS)
	@echo "Coverage Test Files:				" $(COVERAGE_TEST_OBJ)


