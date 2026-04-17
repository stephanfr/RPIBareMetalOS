# Coding Agents Guide

## Type Naming Convention (All Projects)

Use consistent, lowercase `snake_case` for new type names across the repository.

### Rules
- **Primary type names** (classes, structs, enums, aliases) use lowercase `snake_case` (e.g., `block_header`, `lru_cache`, `binary_semaphore_array`).
- **Type aliases** should be descriptive and follow the same convention, often ending in `_type` or `_allocator_type` when it clarifies intent (e.g., `cache_entry_type`, `map_entry_allocator_type`).
- **Type-trait aliases** may use the standard C++ `_t` suffix (e.g., `common_type_t`, `remove_cvref_t`).
- **Avoid CamelCase** for new type names unless matching a standard-library type or existing external API.

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

## Pre-Commit Cleanup

Always remove temporary, debug, and junk files before creating any commit.

### Rules
- **Never commit temp/debug output**: Files such as `gdb_output*.txt`, `core`, `*.log`, `*.tmp`, scratch files, or any file created during a debugging session must be deleted before committing.
- **Inspect before staging**: Run `git status` and review all untracked and modified files. Remove anything that is not intentional source, documentation, or build configuration.
- **Use `.gitignore`**: If a category of temp file recurs, add a pattern to `.gitignore` rather than relying on manual cleanup each time.
- **No "I'll clean it up later" commits**: Every commit should leave the repository in a clean, reviewable state with no leftover investigation artifacts.
