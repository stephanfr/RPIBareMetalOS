<<<<<<< HEAD
include ../Makefile.mk


CPP_TEST_SRC_DIR := test
TEST_OBJ_DIR := test/build
COVERAGE_OBJ_DIR := test/coverage

TEST_EXE := $(TEST_OBJ_DIR)/test.exe
CPP_TEST_SRC := $(wildcard $(CPP_TEST_SRC_DIR)/*.cpp)
TEST_OBJ := $(CPP_TEST_SRC:$(CPP_TEST_SRC_DIR)/%.cpp=$(TEST_OBJ_DIR)/%.o)

COVERAGE_EXE := $(COVERAGE_OBJ_DIR)/coverage.exe
COVERAGE_OBJ := $(CPP_TEST_SRC:$(CPP_TEST_SRC_DIR)/%.cpp=$(COVERAGE_OBJ_DIR)/%.o)

CDEFINES := -D__MINIMAL_STD_TEST__
INCLUDE_DIRS += -I. -I$(CATCH2_PATH)/include

TEST_LIB := -L$(CATCH2_PATH)/lib -lCatch2Main -lCatch2


test : clean_test $(TEST_EXE)

test-coverage : clean_test $(COVERAGE_EXE)
	cd $(COVERAGE_OBJ_DIR)
	gcov ../list_test.cpp --object-directory .
	lcov --capture --directory . --output-file $(COVERAGE_OBJ_DIR)/test_coverage.info
	lcov --remove $(COVERAGE_OBJ_DIR)/test_coverage.info '/usr/include/*' '$(CATCH2_PATH)/*' --output-file $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info
	genhtml $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info --output-directory $(COVERAGE_OBJ_DIR)/coverage_report

$(TEST_EXE) : $(TEST_OBJ)
	$(TEST_LD) $(TEST_OBJ) $(TEST_LIB) -o $(TEST_EXE)
	./$(TEST_EXE)

$(TEST_OBJ_DIR)/%.o: $(CPP_TEST_SRC_DIR)/%.cpp
	$(TEST_CC) $(INCLUDE_DIRS) $(TEST_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(COVERAGE_EXE) : $(COVERAGE_OBJ)
	$(COVERAGE_LD) $(COVERAGE_OBJ) -o $(COVERAGE_EXE) $(TEST_LIB) -lgcov
	./$(COVERAGE_EXE)

$(COVERAGE_OBJ_DIR)/%.o: $(CPP_TEST_SRC_DIR)/%.cpp
	$(COVERAGE_CC) $(INCLUDE_DIRS) $(COVERAGE_CPP_FLAGS) $(CDEFINES) -c $< -o $@

clean_test:
	/bin/rm -rf $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/rm -rf $(COVERAGE_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(COVERAGE_OBJ_DIR) > /dev/null 2> /dev/null || true

=======
# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TEST_TARGETS := test test-coverage

ifeq (,$(filter $(TEST_TARGETS), $(MAKECMDGOALS)))
include Makefile.aarch64.mk
else
include Makefile.test.mk
endif
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
