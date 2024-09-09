# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include Makefile.x64.mk

CPP_TEST_SRC_DIR := test
TEST_OBJ_DIR := test/build
OBJ_DIR := test/build
COVERAGE_TEST_OBJ_DIR := test/coverage
COVERAGE_OBJ_DIR := test/coverage

TEST_EXE := $(TEST_OBJ_DIR)/cpputest_stdlib_main.exe
CPP_TEST_SRC := $(wildcard $(CPP_TEST_SRC_DIR)/*.cpp)
TEST_OBJ := $(CPP_TEST_SRC:$(CPP_TEST_SRC_DIR)/%.cpp=$(TEST_OBJ_DIR)/%.o)
OBJ := $(CPP_SRC:$(CPP_SRC_DIR)/%.cpp=$(TEST_OBJ_DIR)/%.o)

COVERAGE_EXE := $(COVERAGE_TEST_OBJ_DIR)/coverage.exe
COVERAGE_TEST_OBJ := $(CPP_TEST_SRC:$(CPP_TEST_SRC_DIR)/%.cpp=$(COVERAGE_TEST_OBJ_DIR)/%.o)
COVERAGE_OBJ := $(CPP_SRC:$(CPP_SRC_DIR)/%.cpp=$(COVERAGE_TEST_OBJ_DIR)/%.o)

CDEFINES := -D__MINIMAL_STD_TEST__
INCLUDE_DIRS := -I. -Iinclude -I$(CPPUTEST_PATH)/include $(INCLUDE_DIRS)

LDLIBS := -L../minimalclib/lib/x64 -lminimalclib

TEST_LIB := -L$(CPPUTEST_PATH)/lib -lCppUTest -lCppUTestExt


test : clean clean_test lib $(TEST_EXE)

test-coverage : clean_test $(COVERAGE_EXE)
	cd $(COVERAGE_TEST_OBJ_DIR)
	gcov ../cpputest_main.cpp --object-directory .
	lcov --capture --directory . --output-file $(COVERAGE_TEST_OBJ_DIR)/test_coverage.info
	lcov --remove $(COVERAGE_TEST_OBJ_DIR)/test_coverage.info '/usr/include/*' '$(CPPUTEST_PATH)/*' --output-file $(COVERAGE_TEST_OBJ_DIR)/test_coverage_filtered.info
	genhtml $(COVERAGE_TEST_OBJ_DIR)/test_coverage_filtered.info --output-directory $(COVERAGE_TEST_OBJ_DIR)/coverage_report

$(TEST_EXE) : $(OBJ) $(TEST_OBJ)
	$(LD) $(OBJ) $(TEST_OBJ) $(LDLIBS) $(TEST_LIB) -o $(TEST_EXE)
	-./$(TEST_EXE)

$(TEST_OBJ_DIR)/%.o: $(CPP_TEST_SRC_DIR)/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(TEST_OPTIMIZATION_FLAGS) $(TEST_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(OBJ_DIR)/%.o: $(CPP_SRC_DIR)/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(TEST_OPTIMIZATION_FLAGS) $(TEST_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(COVERAGE_EXE) : $(COVERAGE_OBJ) $(COVERAGE_TEST_OBJ)
	$(LD) $(COVERAGE_OBJ) $(COVERAGE_TEST_OBJ) $(LDLIBS) $(TEST_LIB) -lgcov -o $(COVERAGE_EXE)
	./$(COVERAGE_EXE)

$(COVERAGE_TEST_OBJ_DIR)/%.o: $(CPP_TEST_SRC_DIR)/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(COVERAGE_OPTIMIZATION_FLAGS) $(COVERAGE_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(COVERAGE_OBJ_DIR)/%.o: $(CPP_SRC_DIR)/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(COVERAGE_OPTIMIZATION_FLAGS) $(COVERAGE_CPP_FLAGS) $(CDEFINES) -c $< -o $@

clean_test:
	/bin/rm -rf $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/rm -rf $(COVERAGE_TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(COVERAGE_TEST_OBJ_DIR) > /dev/null 2> /dev/null || true

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

