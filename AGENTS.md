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
