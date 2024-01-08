include ../Makefile.mk

ASM_SRC_DIR := src/c
C_SRC_DIR := src/c
OBJ_DIR := build
LIB_DIR := lib

LIB := $(LIB_DIR)/libminimalstdio.a
ASM_SRC := $(wildcard $(ASM_SRC_DIR)/*.S)
ASM_OBJ := $(ASM_SRC:$(ASM_SRC_DIR)/%.S=$(OBJ_DIR)/%.o)
C_SRC := $(wildcard $(C_SRC_DIR)/*.c)
C_OBJ := $(C_SRC:$(C_SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

CDEFINES = -D_LIBC_LIMITS_H_ -DPRINTF_ALIAS_STANDARD_FUNCTION_NAMES_SOFT=1
INCLUDE_DIRS += -Iinclude -I../minimalclib/include

TEST_SRC_DIR := test
TEST_OBJ_DIR := test/build
COVERAGE_OBJ_DIR := test/coverage

TEST_EXE := $(TEST_OBJ_DIR)/test.exe
TEST_SRC := $(wildcard $(TEST_SRC_DIR)/*.cpp)
TEST_OBJ := $(TEST_SRC:$(TEST_SRC_DIR)/%.cpp=$(TEST_OBJ_DIR)/%.o) $(C_SRC:$(C_SRC_DIR)/%.c=$(TEST_OBJ_DIR)/%.o)

COVERAGE_EXE := $(COVERAGE_OBJ_DIR)/coverage.exe
COVERAGE_OBJ := $(TEST_SRC:$(TEST_SRC_DIR)/%.cpp=$(COVERAGE_OBJ_DIR)/%.o) $(C_SRC:$(C_SRC_DIR)/%.c=$(COVERAGE_OBJ_DIR)/%.o)

INCLUDE_DIRS += -I$(CATCH2_PATH)/include

TEST_LIB := -L$(CATCH2_PATH)/lib -lCatch2Main -lCatch2


all: clean $(LIB)


$(LIB) : $(C_OBJ) $(ASM_OBJ)
	$(AR) rcs $(LIB) $(ASM_OBJ) $(C_OBJ)

$(OBJ_DIR)/%.o: $(ASM_SRC_DIR)/%.S 
	$(CC) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(C_SRC_DIR)/%.c
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) $(CDEFINES) -c $< -o $@

clean:
	/bin/rm $(LIB_DIR)/*.* $(OBJ_DIR)/*.o > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(LIB_DIR) > /dev/null 2> /dev/null || true



test: clean_test $(TEST_EXE)


test-coverage : clean_test $(COVERAGE_EXE)
	cd $(COVERAGE_OBJ_DIR)
	gcov ../list_test.cpp --object-directory .
	lcov --capture --directory . --output-file $(COVERAGE_OBJ_DIR)/test_coverage.info
	lcov --remove $(COVERAGE_OBJ_DIR)/test_coverage.info '/usr/include/*' '$(CATCH2_PATH)/*' --output-file $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info
	genhtml $(COVERAGE_OBJ_DIR)/test_coverage_filtered.info --output-directory $(COVERAGE_OBJ_DIR)/coverage_report


$(TEST_EXE) : $(TEST_OBJ)
	$(TEST_LD) $(TEST_OBJ) $(TEST_LIB) -o $(TEST_EXE)
	./$(TEST_EXE)

$(TEST_OBJ_DIR)/%.o: $(TEST_SRC_DIR)/%.cpp
	$(TEST_CC) $(INCLUDE_DIRS) $(TEST_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: $(ASM_SRC_DIR)/%.S 
	$(TEST_CC) $(TEST_CPP_FLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: $(C_SRC_DIR)/%.c
	$(TEST_CC) $(INCLUDE_DIRS) $(TEST_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(COVERAGE_EXE) : $(COVERAGE_OBJ)
	$(COVERAGE_LD) $(COVERAGE_OBJ) $(TEST_LIB) -lgcov -o $(COVERAGE_EXE)
	./$(COVERAGE_EXE)

$(COVERAGE_OBJ_DIR)/%.o: $(TEST_SRC_DIR)/%.cpp
	$(COVERAGE_CC) $(INCLUDE_DIRS) $(COVERAGE_CPP_FLAGS) $(CDEFINES) -c $< -o $@

$(COVERAGE_OBJ_DIR)/%.o: $(ASM_SRC_DIR)/%.S 
	$(COVERAGE_CC) $(COVERAGE_CPP_FLAGS) -c $< -o $@

$(COVERAGE_OBJ_DIR)/%.o: $(C_SRC_DIR)/%.c
	$(COVERAGE_CC) $(INCLUDE_DIRS) $(COVERAGE_CPP_FLAGS) $(CDEFINES) -c $< -o $@


clean_test:
	/bin/rm -rf $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/rm -rf $(COVERAGE_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(TEST_OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(COVERAGE_OBJ_DIR) > /dev/null 2> /dev/null || true
