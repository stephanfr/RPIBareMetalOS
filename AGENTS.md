# Coding Agents Guide

## Type Naming Convention

Naming follows the layer a type lives in.  The libraries under `deps/` are standard-library
shaped, so they use `snake_case`.  The OS under `rpibaremetalos/` uses CamelCase for its own
domain types -- 167 CamelCase types to 7 snake_case, and all 50+ enums CamelCase.

### Libraries -- `deps/` (minimalclib, minimalstdio, minimalstdlib, fat32filesystem, baremetalbase)

- **Primary type names** (classes, structs, enums, aliases) use lowercase `snake_case` (e.g., `block_header`, `lru_cache`, `binary_semaphore_array`).
- **Type aliases** should be descriptive and follow the same convention, often ending in `_type` or `_allocator_type` when it clarifies intent (e.g., `cache_entry_type`, `map_entry_allocator_type`).
- **Type-trait aliases** may use the standard C++ `_t` suffix (e.g., `common_type_t`, `remove_cvref_t`).
- **Avoid CamelCase** for new type names unless matching a standard-library type or existing external API.

### OS -- `rpibaremetalos/`

- **Classes, structs and enums** use `CamelCase` (e.g., `PlatformInfo`, `SDCardController`, `SchedulerClock`, `HostType`).  Acronyms stay upper-case: `EMMCControllerType`, `BCM2711ExceptionManager`, `RPI5PlatformInfo`.
- **Enums** are declared `typedef enum class Name : uint32_t { ... } Name;` with **ALL_CAPS values** (e.g., `LogLevel::DEBUG_1`, `MemoryModelTypes::KERNEL_ONLY_1_TO_1`).
- **Methods** are `PascalCase`.  **Member variables** are `snake_case_` with a trailing underscore.  **Local variables** are `snake_case`.
- **File names** are `snake_case`, named for the primary type they declare (`platform_info.h` holds `PlatformInfo`).
- **snake_case remains correct for library-style types written inside the OS** -- allocators and standard-library adapters (`dynamic_allocator`, `static_allocator`, `xoroshiro_random_device`, `numeric_limits`) -- and for names mirroring an external API (`iproc_rng200_dev`, after Linux's `iproc-rng200`).

## Class `static constexpr` Constants

### Rules
- **Use ALL_CAPS with underscores** for class `static constexpr` constants (e.g., `WORD_BITS`, `TAIL_MASK`).

### Examples
- `block_header`
- `cache_entry_type`
- `map_entry_allocator_type`
- `common_type_t`

## Class Member Ordering

### Rules
- **Private nested types first**: When a class contains private nested structs or classes (implementation details like nodes, blocks, arenas), place them in a `private:` section at the top of the class, before the `public:` section.
- **Public API second**: The `public:` section with constructors, destructors, and public methods follows the private types.
- **Private members and methods last**: A second `private:` section contains member variables, helper methods, and virtual method overrides.
- **Public nested types are an exception**: When a nested type is part of the public API (e.g., iterators, tokens, entries), it belongs in the `public:` section.

### Layout
```cpp
class example_resource
{
private:
    // Constants
    static constexpr size_t SOME_CONSTANT = 64;

    // Private nested structs/classes (implementation details)
    struct internal_node { ... };
    struct internal_block { ... };

public:
    // Constructors, destructor, public methods
    example_resource(...);
    ~example_resource();
    size_t size() const noexcept;

private:
    // Member variables
    size_t count_;

    // Helper methods
    void do_something();

    // Virtual method overrides
    void *do_allocate(size_t bytes, size_t alignment) override;
};
```

### Reference Files
- `lockfree_single_arena_resource.h`, `avl_tree`, `list`, `forward_list`

## Git Workflow

### Rules
- **Always work in a branch**: Never commit directly to `main`. Create a feature branch for every set of changes so that all work flows through a GitHub pull request.
- **Never commit unless explicitly told to**: Do not stage or commit changes without an explicit instruction from the user to do so. Building, testing, and editing files is fine at any time; committing is not.

## Pre-Commit Cleanup

Always remove temporary, debug, and junk files before creating any commit.

### Rules
- **Never commit temp/debug output**: Files such as `gdb_output*.txt`, `core`, `*.log`, `*.tmp`, scratch files, or any file created during a debugging session must be deleted before committing.
- **Inspect before staging**: Run `git status` and review all untracked and modified files. Remove anything that is not intentional source, documentation, or build configuration.
- **Use `.gitignore`**: If a category of temp file recurs, add a pattern to `.gitignore` rather than relying on manual cleanup each time.
- **No "I'll clean it up later" commits**: Every commit should leave the repository in a clean, reviewable state with no leftover investigation artifacts.

## Unit Test Creation

Tests live in `rpibaremetalos/test/src/` and use the CppUTest framework.  They run natively
(x86-64 host) via `make test` from `rpibaremetalos/`.

### File Template

```cpp
// Copyright <year> Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"        //  Always first -- pulls in TestHarness + heap helpers

#include "path/to/header_under_test.h"

//  Additional includes as needed (e.g. <string.h>, <limits>, "heaps.h")

namespace
{
    //  …all test groups and tests…
}
```

Every test file is wrapped in an anonymous namespace.  Files are auto-discovered by the
Makefile (`wildcard test/src/*.cpp`), so no Makefile change is needed to add a new test
file.  However, if the module under test is not already compiled into the test binary,
add its `.cpp` to `CPP_SRC` (or `C_SRC`) in `Makefile.test.mk`.

### Test Groups

One `TEST_GROUP` per logical aspect of the API (constants, parsing, formatting,
comparison, copy semantics, etc.).  Wrap each group in the diagnostic pragmas because
CppUTest generates a variable that triggers `-Wunused-variable`:

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MyFeatureTests)
    {};
#pragma GCC diagnostic pop
```

### Naming Conventions

- **Group names**: `CamelCase` + `Tests` suffix, named for the feature being tested
  (e.g., `UUIDConstantTests`, `ParseHexUint32PrefixTests`, `PointerResultExpandedTests`).
- **Test names**: `PascalCase` verb phrase describing the *behavior* under test
  (e.g., `NumericDigitsMapToTheirValue`, `ReseedingReproducesFirstUuid`,
  `TrichotomyHoldsAcrossGeneratedSet`).  Avoid names that describe a single input value;
  name the behavior instead.

### Grouping Principle -- One Test Per Behavior, Not Per Input

This is the most important rule.  A test should verify one *behavior* and may contain
multiple `CHECK_*` calls covering several related inputs.  Do **not** create one test
per input value.

**Good** -- one behavior, many inputs:
```cpp
TEST(ParseHexUint32StopTests, StopsAtWhitespaceAndControlChars)
{
    CHECK_EQUAL(0x1A, ParseHexUint32("1A BC"));
    CHECK_EQUAL(0x1A, ParseHexUint32("1A\tBC"));
    CHECK_EQUAL(0x1A, ParseHexUint32("1A\nBC"));
    CHECK_EQUAL(0x1A, ParseHexUint32("1A\rBC"));
}
```

**Good** -- exhaustive range via loop:
```cpp
TEST(HexDigitValueTests, NumericDigitsMapToTheirValue)
{
    for (char c = '0'; c <= '9'; ++c)
    {
        CHECK_EQUAL(c - '0', HexDigitValue(c));
    }
}
```

**Bad** -- one test per input:
```cpp
TEST(HexDigitValueTests, DigitZeroReturnsZero)  { CHECK_EQUAL(0, HexDigitValue('0')); }
TEST(HexDigitValueTests, DigitOneReturnsOne)    { CHECK_EQUAL(1, HexDigitValue('1')); }
// …ten more identical tests…
```

When in doubt, ask: *if this test fails, does the failure message tell me which specific
input was wrong?*  If yes, the inputs belong in the same test.  `CHECK_EQUAL` prints both
expected and actual values, so the failing line identifies the exact input.

### Assertions

| Macro | Use for | Example |
|---|---|---|
| `CHECK_EQUAL(a, b)` | Numeric equality (most common) | `CHECK_EQUAL(42, result.Value());` |
| `CHECK_TRUE(expr)` | Boolean true | `CHECK_TRUE(result.Successful());` |
| `CHECK_FALSE(expr)` | Boolean false | `CHECK_FALSE(a == b);` |
| `CHECK(expr)` | General truthiness | `CHECK(a != b);` |
| `STRCMP_EQUAL(a, b)` | C-string equality | `STRCMP_EQUAL("de:ad:be:ef:01:23", buf);` |
| `POINTERS_EQUAL(a, b)` | Pointer identity | `POINTERS_EQUAL(buf, ret);` |

**Enum comparisons**: cast both sides to `int` (or the underlying type) to avoid
CppUTest printing the enum name instead of the value:
```cpp
CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::SUCCESS),
            static_cast<int>(result.ResultCode()));
```

### What to Cover

For every class or function under test, aim for these categories (skip any that don't
apply):

1. **Constants and defaults** -- static members, default-constructed state, sentinel
   values (e.g., `NIL`, `MAX`, all-zeros).
2. **Core functionality** -- the primary operation with representative inputs.
3. **String / formatting** -- `ToString`, buffer lengths, dash positions, case.
4. **Bit-level invariants** -- version bits, variant bits, flag fields.  Verify both
   the accessor method *and* the string representation agree.
5. **Comparison operators** -- reflexivity (`a == a`, `!(a < a)`), ordering between
   known sentinels, trichotomy (for every pair exactly one of `<`, `==`, `>` holds),
   anti-symmetry (`a < b` iff `b > a`).
6. **Determinism** -- seed → generate → record; re-seed → generate → compare.  Also
   verify that `SeedRNG(0)` or equivalent falls back to a default.
7. **Copy / move semantics** -- copy constructor, assignment, move constructor,
   move assignment.  Verify the value survives and (for move) the source is valid.
8. **Edge cases** -- empty input, max-length input, overflow / wrap-around, leading
   zeros, sign characters, whitespace-only.
9. **Error / failure paths** -- failure codes, null pointers, missing entities.
10. **Integration / real-world patterns** -- the way the code is actually called in
    production (chained results, sequential operations).

### Build Environment Constraints

The test build uses a non-standard toolchain configuration.  Be aware of these
limitations:

- **`-DCPPUTEST_USE_STD_C_LIB=0`** -- the standard C library is *not* linked.
  `strlen`, `strchr`, etc. are unavailable.  Use the minimalclib equivalents declared
  in `deps/minimalclib/include/string.h` (e.g., `strnlen`, `strncmp`, `strtoklit`).
  Include them with `#include <string.h>` (resolves to minimalclib's header via
  `-I../deps/minimalclib/include`).
- **`-fno-exceptions -fno-rtti`** -- no `try/catch`, no `dynamic_cast` on polymorphic
  types at runtime (compile-time casts are fine).
- **`-D__NO_LOGGING__`** -- logging macros compile to nothing.
- **Heap resources** -- `__os_dynamic_heap_resource`, `__os_static_heap_resource`,
  `__os_filesystem_cache_heap_resource` are defined in `cpputest_main.cpp`.  Do not
  redefine them in test files.  If you need `dynamic_new<T>()` (from `heaps.h`),
  include `heaps.h` which declares the resources `extern` -- but do **not** also write
  your own `extern` declaration in the test file, as the by-value vs by-reference
  forms conflict.
- **`putchar_`** -- stubbed in `cpputest_main.cpp`; required by minimalstdio's `printf`.
  No action needed in test files.

### Validation

After creating or modifying tests, always run:

```bash
cd rpibaremetalos && make test
```

The full suite must pass.  To run only the new test group during development:

```bash
./test/build/cpputest_main.exe -g MyNewGroupTests
```
